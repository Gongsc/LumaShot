<div align="center">
  <img src="src/LumaShot.App/assets/LumaShot.png" alt="LumaShot 应用图标" width="128">
  <h1>LumaShot</h1>
  <p>面向 HDR 及 HDR/SDR 混合桌面的 Windows 11 原生截图工具。</p>
  <p><a href="README.md">English</a> · <strong>简体中文</strong></p>
  <p><code>v0.1.1</code> · Windows 11 · x64</p>
</div>

## 项目现状

**v0.1.1 是可用的预览版本，项目仍在积极开发。** 截图、标注、HDR 校准、剪贴板和文件导出等核心流程已经实现，并通过自动化测试及真实 Windows Graphics Capture 冒烟测试。目前发布产物尚未签名。

## 主要功能

- 区域、窗口、当前显示器及全部显示器截图
- 原生 16 位浮点 scRGB 捕获，支持 HDR/SDR、不同缩放比例混用
- 全屏 HDR 到 SDR 校准，浮动面板调整并实时预览
- 画笔、矩形、箭头及支持输入法的文字标注，可撤销与重做
- 无损 HDR JPEG XR 与色调映射 sRGB PNG 输出
- 兼容 SDR 软件的 PNG、`CF_DIBV5` 和 `CF_DIB` 剪贴板数据
- 系统托盘、可配置全局快捷键及可选的按 `Enter` 复制
- 简体中文与英文界面

## 使用方法

启动 `LumaShot.exe` 会打开控制中心，并在通知区域启动原生截图引擎。双击托盘图标或按全局快捷键（默认 `Ctrl+Shift+PrintScreen`），选择截图模式，调整选区或添加标注，然后复制或保存。HDR 选区默认保存为 JPEG XR；PNG 和剪贴板会转换为 SDR，以兼容常用应用。

键盘快捷键：开启选项后按 `Enter` 复制，`Ctrl+C` 复制，`Ctrl+S` 保存，`Ctrl+Z` 撤销，`Ctrl+Y` 重做，`Esc` 取消。

## 环境与构建

- Windows 11 22H2 或更高版本，x64
- Visual Studio 2022 Build Tools、使用 C++ 的桌面开发组件及 Windows 11 SDK
- .NET 10 SDK
- 仅制作安装程序时需要 Inno Setup 6

```powershell
.\scripts\build.ps1 -Configuration Release -RunTests
.\scripts\package.ps1 -Version 0.1.1
```

打包后会在 `artifacts` 中直接生成自包含的 `LumaShot.exe` 和 `LumaShot-v<版本>-Setup-x64.exe`。独立版必须保留 `LumaShot.exe` 这一规范文件名，WinUI 资源依赖该宿主名称；不会生成 ZIP 压缩包。

技术细节请参阅 [HDR 指南](docs/HDR-Guide.md)和[测试矩阵](docs/TESTING.md)。受保护内容、DRM 内容及 Windows 安全桌面可能显示为黑色；LumaShot 不会绕过操作系统的捕获限制。

## 许可证

LumaShot 仅依据 [GNU 通用公共许可证第 3 版](LICENSE)（`GPL-3.0-only`）授权。
