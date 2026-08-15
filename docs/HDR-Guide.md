# HDR 使用说明 / HDR Guide

## 中文

LumaShot 在内部始终使用 `R16G16B16A16_FLOAT` 线性 scRGB。截图涉及已开启 HDR 的显示器时，“保存”对话框默认选择 JPEG XR（`.jxr`）；该格式保留浮点像素以及超过 SDR 白点的高光。Windows Photos 可用于查看 HDR JXR，显示器需在 Windows“系统 > 显示 > HDR”中开启 HDR。

截图工具栏中的“HDR 校准”和“设置 > HDR 校准”均提供输出亮度与高光压缩控制。截图界面会直接实时更新当前捕获内容，设置页则优先使用当前 HDR 屏幕的缩略图预览。调整仅作用于 SDR 截图预览、PNG 和剪贴板；JPEG XR 始终保存未经校准压缩的原始浮点 HDR 数据。默认值为 85% 输出亮度和 20% 高光压缩，可随时恢复默认。

PNG 和剪贴板输出始终是 SDR。程序使用 Direct2D HDR Tone Map、White Level Adjustment 和色彩管理转换为 8 位 sRGB，因此可以粘贴到画图、Office 和常见聊天软件。此行为不会改变仍在内存中的 HDR 原图；保存失败时可直接重试或改选另一格式。

混合 HDR/SDR 显示器截图会合成到统一 scRGB 画布。程序通过 DXGI 与 DisplayConfig 记录显示器的 HDR 状态、峰值亮度和 SDR 白电平，并使用物理像素坐标处理负坐标与不同 DPI。若需要验证 HDR 是否保留，可用支持浮点 JPEG XR 的解码器检查像素，图像中应能存在大于 `1.0` 的通道值。

受 DRM、企业策略保护的窗口或 Windows 安全桌面可能返回黑色画面，这是系统捕获限制；LumaShot 不会绕过这些限制。

## English

LumaShot keeps captured content in linear `R16G16B16A16_FLOAT` scRGB. When a selection touches an HDR-enabled display, Save defaults to JPEG XR (`.jxr`), preserving floating-point samples and highlights above SDR white. Windows Photos can display HDR JXR when HDR is enabled under Windows Settings > System > Display > HDR.

HDR calibration is available from both the capture toolbar and Settings. The capture overlay updates the captured content directly as the sliders move, while Settings uses a live thumbnail of the current HDR display when available. Calibration affects SDR capture preview, PNG, and clipboard output only; JPEG XR always keeps the original floating-point HDR pixels. Defaults are 85% output brightness and 20% highlight compression.

PNG and clipboard output are always SDR. A Direct2D HDR Tone Map, White Level Adjustment, and color-management chain converts the image to 8-bit sRGB for compatibility with Paint, Office, and common messaging apps. The in-memory HDR capture is retained when export fails, so saving can be retried or switched to another format.

Mixed HDR/SDR captures are composed on a single scRGB canvas. DXGI and DisplayConfig provide HDR state, peak luminance, and SDR white level, while physical-pixel coordinates preserve negative desktop origins and mixed DPI. To validate an HDR file, decode the floating-point JXR and confirm that one or more channel values remain above `1.0`.

DRM-protected content, policy-protected windows, and the Windows secure desktop can appear black because of operating-system capture restrictions. LumaShot does not bypass them.
