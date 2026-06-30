# Changelog

本文件记录看穿 (KanChuan) 的版本变更历史。格式基于 [Keep a Changelog](https://keepachangelog.com/)。

## [1.1.0] - 2026-06-30

### Added

- 大文件分块传输：超过 10MB 的文件自动分块编码，兼容 cimbar-bigfile manifest 协议
- 设置对话框：可调帧率（5-20fps）、冗余倍数（1.2-8.1x）、chunk 大小等参数
- 调试日志（dlog）：编码/解码关键步骤记录

### Changed

- OpenCV 升级至 4.12.0，精简构建 7 模块（core/imgproc/calib3d/features2d/flann/imgcodecs/photo）+ LTO
- DLL 体积从 24.96 MB 减至 22.02 MB（-11.8%）

### Fixed

- 部分已知问题

## [1.0.0] - 2026-06-01

### Added

- 基于 libcimbar 的 Windows 移植版，aardio GUI
- 文件编码为 cimbar 动态图像序列
- 摄像头实时解码还原文件
- 支持 CFC 手机端扫描解码
