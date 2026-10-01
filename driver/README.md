# Yuanqin Virtual HID Keyboard

`YuanqinVhid` 是独立的 KMDF/VHF 软件设备驱动。GUI 通过自定义设备接口发送
最多六个同时按下的字母键，驱动再向 Windows HID 栈提交标准键盘报告。驱动不会
修改或注入任何游戏进程，也不会隐藏自身的虚拟设备属性。

## 构建要求

- Visual Studio 2022 的“使用 C++ 的桌面开发”；
- Windows 11 SDK；
- 与 SDK 版本匹配的 Windows Driver Kit（WDK）。

在 Developer PowerShell 中构建：

```powershell
msbuild driver\YuanqinVhid\YuanqinVhid.vcxproj /p:Configuration=Debug /p:Platform=x64
```

仓库根目录的普通 CMake 构建只构建 GUI 和用户态客户端，不会构建内核驱动。

## 开发机安装

驱动安装会修改系统设备配置，必须在专门的测试环境中进行。Debug 驱动需要有效的
测试签名；64 位 Windows 默认不会加载未签名内核驱动。启用测试签名通常还涉及
Secure Boot，请按 Microsoft 的官方驱动测试签名文档操作，不要使用来源不明的签名
绕过工具。

完成构建和测试签名后，以管理员身份使用 WDK 自带的 `devcon` 创建根枚举设备：

```powershell
devcon install driver\YuanqinVhid\bin\x64\Debug\YuanqinVhid.inf Root\YuanqinVhid
```

安装成功后，设备管理器的“系统设备”中应出现 `Yuanqin Virtual HID Keyboard`，
“键盘”分类下还会出现由 VHF 创建的 HID Keyboard Device。随后启动 GUI 即可；GUI
不会在驱动缺失时退回 `SendInput`。

正式分发前必须完成微软要求的生产驱动签名与兼容性测试。游戏仍可能拒绝虚拟输入
设备，因此先用 Debug 驱动做单机验证，再决定是否投入正式签名流程。
