# 看穿 (KanChuan)

基于 [libcimbar](https://github.com/sz3/libcimbar) 的 Windows 移植版，使用 [aardio](https://www.aardio.com/) 开发 GUI 界面。

将任意文件编码为动态彩色条形码图像序列，通过摄像头实时解码还原文件——实现"看穿屏幕"的文件传输。

![看穿截图](ScreenShot.png)

## 功能

- **文件编码**：将文件编码为 cimbar 动态图像序列，循环播放
- **划区截图**：截取屏幕区域进行编码
- **图像解码**：调用系统摄像头实时捕获 cimbar 图像并解码还原
- **摄像头预览**：解码模式下显示摄像头实时画面，四角锚点指示器辅助对齐
- **镜像翻转**：双击画布切换左右镜像，方便前置摄像头用户
- **拖放支持**：直接拖放文件到窗口开始编码
- **多模式**：支持 B / Bu / Bm / 4C 四种编码模式

## 用法

1. **编码**：点击"打开文件"选择文件，或直接拖放文件到窗口。程序自动生成动态图像序列循环播放。
2. **截图编码**：点击"划区截图"，框选屏幕区域进行编码。
3. **解码**：点击"图像解码"，启动摄像头。将摄像头对准另一台设备上播放的 cimbar 动态图像，四角锚点变绿表示对齐成功，程序自动解码并保存文件。

## 项目结构

```
├── main.aardio          # 主程序（GUI + 编码/解码/画布逻辑）
├── lib/
│   ├── cimbar.aardio    # DLL 封装模块
│   └── config.aardio    # 配置模块
├── dll/
│   ├── cimbar_dll.dll   # 核心编解码 DLL（32位，静态链接 OpenCV + Media Foundation）
│   └── libwinpthread-1.dll  # MinGW 运行时
├── BUILD_DLL.md         # DLL 编译指南
├── libcimbar-0.6.5/     # 原项目源码（含修改）
├── res/                 # 资源文件
└── default.aproj        # aardio 项目文件
```

## 编译

如需重新编译 DLL，请参考 [BUILD_DLL.md](BUILD_DLL.md)。

## 协议

本项目遵循原项目 [libcimbar](https://github.com/sz3/libcimbar) 的开源协议 **Mozilla Public License 2.0 (MPL-2.0)**。

- 原项目源码位于 `libcimbar-0.6.5/` 目录，保留原始协议声明
- DLL 修改代码位于 `build/` 目录，同样遵循 MPL-2.0
- aardio GUI 代码遵循 MPL-2.0

## 致谢

- [sz3/libcimbar](https://github.com/sz3/libcimbar) — 原始 cimbar 编解码库
- [OpenCV](https://opencv.org/) — 计算机视觉库
- [aardio](https://www.aardio.com/) — Windows 快速开发工具
