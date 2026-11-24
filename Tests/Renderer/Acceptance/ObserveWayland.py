"""在独立的嵌套 Wayland 桌面验证 FirstFrame 的像素、resize 和关闭。"""

import json
import os
from pathlib import Path
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


def observe(executable, runtime):
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
            verify_window(executable, tools, loader_debug)
        finally:
            stop_process(compositor)


def verify_window(executable, tools, loader_debug):
    app_env = dict(tools)
    if loader_debug:
        app_env["LD_DEBUG"] = loader_debug
    # stderr 由外层部署验收读取，用于核对应用实际加载的 Pangu 模块。
    progress = Path("first-frame.stdout")
    with progress.open("w") as stream:
        app = subprocess.Popen([executable], env=app_env, stdout=stream)
        try:
            def sample(size, color):
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
                x = rect["x"] + size[0] // 2
                y = rect["y"] + size[1] // 2
                ppm = command(["grim", "-g", f"{x},{y} 1x1", "-t", "ppm", "-"], tools)
                return ppm.startswith(b"P6\n1 1\n255\n") and ppm[-3:] == bytes.fromhex(color)

            def control(action):
                reply = json.loads(command(["swaymsg", "-r", f"[pid={app.pid}] {action}"], tools))
                if not reply or not all(item["success"] for item in reply):
                    raise RuntimeError(f"sway {action}: {reply}")

            wait_for("720x480 #3f99c8", lambda: sample((720, 480), "3f99c8"))
            control("resize set width 560 px height 360 px")
            # 先确认窗口在新尺寸下继续呈现第一种颜色，再触发换色。
            wait_for("560x360 #3f99c8", lambda: sample((560, 360), "3f99c8"))
            Path("second-color").touch()
            wait_for("560x360 #ff0000", lambda: sample((560, 360), "ff0000"))
            wait_for("120 present requests", lambda: "first-frame present-request-progress=120\n"
                     in progress.read_text())
            control("kill")  # 对原生 xdg_toplevel 发送 close。
            if app.wait(timeout=8) != 0:
                raise RuntimeError(f"FirstFrame exit code {app.returncode}")
        finally:
            stop_process(app)
    print("Wayland: 720x480 #3F99C8 -> 560x360 #FF0000; 120 present requests; compositor close verified")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="pg-", dir=os.environ["XDG_RUNTIME_DIR"]) as directory:
        observe(sys.argv[1], Path(directory))
