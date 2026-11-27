# Android 真机唤醒与解锁

在 Windows 上通过已配对的无线 ADB 连接 Android 真机。设备就绪后，可在 PowerShell 中调用脚本唤醒并解锁设备。脚本会保留 scrcpy 投屏窗口。

## 运行前确认

- Android 真机已开启无线调试，且在 ADB 设备列表中的状态为 `device`。
- Windows 已安装 Android SDK Platform-Tools。脚本依次从 `ANDROID_HOME`、`ANDROID_SDK_ROOT` 和 `%LOCALAPPDATA%\Android\Sdk` 确定 SDK 路径。
- `scrcpy.exe` 可通过 `PATH` 调用。设备处于锁屏状态时，还需要 `cua-driver.exe`。

## 唤醒并解锁

从 PanguEngine 仓库根目录运行。仅连接一台无线 ADB 设备时，可以省略设备序列号。

```powershell
.\Scripts\unlock-android.ps1
```

有多台设备时，用 `-Serial` 指定目标。将示例值替换为 `adb devices -l` 所列的实际设备序列号。

```powershell
.\Scripts\unlock-android.ps1 -Serial '192.0.2.10:37123'
```

设备需要 PIN 时，脚本优先使用传入的 `-Pin`（`SecureString`）。省略该参数时，脚本从本地 `.local\credentials\android-device.xml` 读取与设备序列号匹配的凭据。文件缺失或序列号不匹配时，按 PowerShell 提示输入手机的数字锁屏 PIN。

脚本输出「设备已解锁，scrcpy 窗口已打开」后，可在 `Android-scrcpy` 窗口中操作设备。

[脚本源码](../../Scripts/unlock-android.ps1)
