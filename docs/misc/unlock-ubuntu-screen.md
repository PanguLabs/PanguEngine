# Ubuntu 真机桌面解锁与临时保持亮屏

可以通过 SSH 唤醒 `moozen-ubuntu` 的显示器、解锁已登录的 GNOME Wayland 桌面，并临时暂停自动息屏。如果只想点亮显示器以查看锁屏界面，只执行下文的唤醒命令即可。

## 连接并确认本地桌面会话

在 Windows PowerShell 中登录 Ubuntu。本文其余命令均在这次 SSH 连接的 Ubuntu Shell 中执行。

```powershell
ssh -o BatchMode=yes moozen-ubuntu
```

SSH 使用这台 Windows 主机已配置的密钥进行公钥认证。`BatchMode=yes` 会关闭交互提示，需要现场输入口令时连接会失败。

查询物理显示器所在的 `seat0`，取得当前活动会话。会话 ID 可能随登录而变化。

```bash
sid=$(loginctl show-seat seat0 -p ActiveSession --value)
loginctl show-session "$sid" -p Name -p Type -p Active -p LockedHint
```

这台真机的本地桌面应显示 `Name=moozen`、`Type=wayland`、`Active=yes`。`LockedHint=yes` 表示桌面已锁定。

## 唤醒显示器并按需解锁

通过 GNOME 的显示服务点亮屏幕。这一步保留当前锁屏状态。

```bash
busctl --user set-property org.gnome.Mutter.DisplayConfig \
  /org/gnome/Mutter/DisplayConfig org.gnome.Mutter.DisplayConfig PowerSaveMode i 0
cat /sys/class/drm/card1-eDP-1/dpms
```

`On` 表示内置显示器已亮。`card1-eDP-1` 是这台真机实测的接口名。

需要使用桌面时，在同一 SSH 会话中解锁。下面的命令先核对桌面归属，再向 `systemd-logind` 发出解锁请求。

```bash
test -n "$sid" && test "$(loginctl show-session "$sid" -p Name --value)" = "$(id -un)" &&
  loginctl unlock-session "$sid"
loginctl show-session "$sid" -p Active -p LockedHint
```

确认 `Active=yes` 且 `LockedHint=no` 后即可进行桌面操作。这台机器允许已通过 SSH 认证的 `moozen` 解锁自己的会话，实测无需再次输入桌面密码。

### 解锁后重新启动自动锁屏计时

仅通过 SSH 唤醒并解锁时，GNOME 可能沿用已经触发过的空闲周期。确认解锁后，在同一 SSH 会话中执行以下命令。

```bash
gnome-session-inhibit --inhibit idle --reason SSH-unlock sleep 1
```

命令短暂申请空闲抑制。GNOME 在最后一个空闲抑制请求释放时重新开始计时，之后按当前桌面设置自动息屏和锁定。若还有其他空闲抑制请求，等它们释放后才会开始计时。

## 临时保持亮屏

用 `gnome-session-inhibit --inhibit idle` 启动命令，GNOME 会在命令运行期间暂停自动息屏。将示例中的 `sleep 60` 替换为你要执行的命令及其参数。

```bash
gnome-session-inhibit --inhibit idle --reason "临时保持亮屏" sleep 60
```

如果需要在多条命令之间持续保持亮屏，可在单独一个 SSH 终端运行以下命令，并保留该终端。

```bash
gnome-session-inhibit --inhibit idle --reason "临时保持亮屏" --inhibit-only
```

## 撤销保持并检查状态

命令成功或失败退出时，GNOME 都会释放这次亮屏请求。使用 `--inhibit-only` 时，在运行它的终端按 `Ctrl+C` 即可释放。

```bash
gnome-session-inhibit --list
gsettings get org.gnome.desktop.session idle-delay
gsettings get org.gnome.desktop.screensaver lock-enabled
gsettings get org.gnome.desktop.screensaver lock-delay
```

在列表中确认自己的请求已消失。若列表显示 `No inhibitors`，当前没有空闲抑制请求。释放请求后，桌面恢复原有空闲策略。

`idle-delay` 是 GNOME 进入空闲状态的阈值。例如 `uint32 60` 表示 60 秒。`lock-enabled` 控制自动锁屏，`lock-delay` 是进入空闲状态后的配置延迟，单位也是秒。锁屏过渡也会影响实际完成时间。

## 测试结束后锁屏

测试结束后，在取得 `sid` 的 Ubuntu SSH 会话中执行以下命令，立即锁定本地桌面，并确认结果为 `LockedHint=yes`。

```bash
loginctl lock-session "$sid"
loginctl show-session "$sid" -p LockedHint
```
