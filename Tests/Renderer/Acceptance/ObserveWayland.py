"""在独立的嵌套 Wayland 桌面验证呈现像素、调整窗口尺寸和关闭。"""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def wait_for(description, check):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(0.05)
    raise TimeoutError(description)


def ensure_running(process):
    if process.poll() is not None:
        raise RuntimeError(f"{process.args[0]} exited early ({process.returncode})")


def stop_process(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def command(args, env):
    return subprocess.check_output(args, env=env, timeout=5)


def windows(node):
    yield node
    for child in node["nodes"] + node["floating_nodes"]:
        yield from windows(child)


def observe(executable, target, runtime):
    tools = dict(os.environ)
    loader_debug = tools.pop("LD_DEBUG", None)
    parent_display = Path(tools["XDG_RUNTIME_DIR"]) / tools.get("WAYLAND_DISPLAY", "wayland-0")
    # 使用私有 socket 目录，所有 IPC 命令和取色都只作用于测试桌面。
    tools.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY=str(parent_display))
    tools.pop("WAYLAND_SOCKET", None)
    tools.pop("SWAYSOCK", None)
    config = runtime / "sway.conf"
    config.write_text('xwayland disable\ndefault_border none\n'
                      'for_window [app_id="PanguApplication"] floating enable, move position center\n')
    with open("wayland-compositor.log", "w") as log:
        compositor = subprocess.Popen(
            ["sway", "--unsupported-gpu", "-c", str(config)],
            env=dict(tools, WLR_BACKENDS="wayland", WLR_LIBINPUT_NO_DEVICES="1"),
            stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            def sockets():
                ensure_running(compositor)
                ipc = next(runtime.glob("sway-ipc*.sock"), None)
                display = next((p for p in runtime.glob("wayland-*") if p.is_socket()), None)
                if ipc and display:
                    return str(ipc), display.name

            tools["SWAYSOCK"], tools["WAYLAND_DISPLAY"] = wait_for("nested Wayland compositor", sockets)
            verify_window(executable, target, tools, loader_debug)
        finally:
            stop_process(compositor)


def verify_window(executable, target, tools, loader_debug):
    app_env = dict(tools)
    if loader_debug:
        app_env["LD_DEBUG"] = loader_debug
    triangle = target == "FirstTriangle"
    label = "first-triangle" if triangle else "first-frame"
    initial = (720, 480)
    resized = (560, 360)
    # stderr 由外层部署验收读取，用于核对应用实际加载的 Pangu 模块。
    progress = Path(f"{label}.stdout")
    with progress.open("w") as stream:
        app = subprocess.Popen([executable], env=app_env, stdout=stream)
        try:
            def sample(size, positions, expected, tolerance):
                ensure_running(app)
                if f"Adding Vulkan swapchain @ {size[0]}x{size[1]}" not in progress.read_text():
                    return False
                tree = json.loads(command(["swaymsg", "-r", "-t", "get_tree"], tools))
                view = next((w for w in windows(tree) if w.get("pid") == app.pid
                             and w.get("app_id") == "PanguApplication"), None)
                if not view:
                    return False
                rect = view["rect"]
                if (rect["width"], rect["height"]) != size:
                    return False
                width, height = size
                geometry = f'{rect["x"]},{rect["y"]} {width}x{height}'
                ppm = command(["grim", "-g", geometry, "-s", "1", "-t", "ppm", "-"], tools)
                header = f"P6\n{width} {height}\n255\n".encode()
                if not ppm.startswith(header) or len(ppm) != len(header) + width * height * 3:
                    return False
                colors = []
                for x, y in positions:
                    offset = len(header) + (width * (height * y // 100) + width * x // 100) * 3
                    colors.append(ppm[offset:offset + 3])
                if all(abs(a - b) <= tolerance for color, wanted in zip(colors, expected, strict=True)
                       for a, b in zip(color, bytes.fromhex(wanted), strict=True)):
                    return view["id"], " ".join(f"#{color.hex().upper()}" for color in colors)

            def control(action):
                reply = json.loads(command(["swaymsg", "-r", f"[pid={app.pid}] {action}"], tools))
                if not reply or not all(item["success"] for item in reply):
                    raise RuntimeError(f"sway {action}: {reply}")

            def resize():
                control(f"resize set width {resized[0]} px height {resized[1]} px")

            if triangle:
                positions = ((25, 40), (25, 60), (75, 40), (75, 60))
                initial_colors = ("6C6CDA", "A0A095", "000000", "000000")
                updated_colors = ("000000", "000000", "6CDA6C", "A095A0")
                window, pixels = wait_for("initial triangle pixels", lambda: sample(initial, positions, initial_colors, 4))
                print(f"Initial: {initial[0]}x{initial[1]} sway-view={window} sRGB {pixels}")
                Path("second-triangle").touch()
                updated_window, pixels = wait_for("same-size triangle update", lambda: sample(initial, positions, updated_colors, 4))
                print(f"Updated: {initial[0]}x{initial[1]} sway-view={updated_window} sRGB {pixels}")
                resize()
                resized_window, pixels = wait_for("resized triangle pixels", lambda: sample(resized, positions, updated_colors, 4))
                print(f"Resized: {resized[0]}x{resized[1]} sway-view={resized_window} sRGB {pixels}")
                if window != updated_window or window != resized_window:
                    raise RuntimeError("The triangle update or resize replaced the Wayland view")
            else:
                center = ((50, 50),)
                wait_for("720x480 #3f99c8", lambda: sample(initial, center, ("3f99c8",), 0))
                resize()
                # 先确认窗口在新尺寸下继续呈现第一种颜色，再触发换色。
                wait_for("560x360 #3f99c8", lambda: sample(resized, center, ("3f99c8",), 0))
                Path("second-color").touch()
                wait_for("560x360 #ff0000", lambda: sample(resized, center, ("ff0000",), 0))
                print("Wayland: 720x480 #3F99C8 -> 560x360 #FF0000")

            wait_for("120 present requests", lambda: f"{label} present-request-progress=120\n" in progress.read_text())
            control("kill")  # 对原生 xdg_toplevel 发送 close。
            if app.wait(timeout=8) != 0:
                raise RuntimeError(f"{target} exit code {app.returncode}")
            log = progress.read_text()
            if triangle and re.findall(r"Adding Vulkan swapchain @ (\d+x\d+)", log) != ["720x480", "560x360"]:
                raise RuntimeError("The triangle run must create a swapchain only for the initial window and the resize")
            requests = re.findall(rf"{label} present-request-progress=(\d+)", log)
            print(f"{target}: {requests[-1]} present requests; compositor close verified; exit 0")
        finally:
            stop_process(app)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="pg-", dir=os.environ["XDG_RUNTIME_DIR"]) as directory:
        target = sys.argv[2] if len(sys.argv) > 2 else "FirstFrame"
        observe(sys.argv[1], target, Path(directory))
