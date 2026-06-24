# 编译 cimbar_dll.dll 指南

将 libcimbar 编译为 32 位 Windows DLL，供 aardio 通过 `raw.loadDll` 调用。

## 前置条件

| 依赖 | 版本 | 安装路径 |
|------|------|----------|
| MSYS2 | 最新 | `C:\Tools\msys64` |
| MinGW32 工具链 | GCC 14+ | MSYS2 内 `mingw32` 子系统 |
| OpenCV | 4.10.0 (MinGW32 静态库) | `C:\Projects\opencv-dist` |
| CMake | 3.10+ | MSYS2 mingw32 自带 |

### 安装 MinGW32 工具链

```bash
# 在 MSYS2 mingw32 环境中
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake make
```

### 编译 OpenCV 4.10.0 MinGW32 静态库

需要自行从源码编译 OpenCV 的 MinGW32 静态库版本，包含以下模块：
- core, imgproc, imgcodecs, calib3d, photo, features2d, flann

编译后的目录结构应为：
```
C:\Projects\opencv-dist\
├── include\opencv4\opencv2\...
└── x64\mingw\staticlib\
    ├── libopencv_core4100.a
    ├── libopencv_imgproc4100.a
    ├── libopencv_imgcodecs4100.a
    ├── libopencv_calib3d4100.a
    ├── libopencv_photo4100.a
    ├── libopencv_features2d4100.a
    ├── libopencv_flann4100.a
    └── ... (其他依赖库)
```

## 中文路径问题

**关键问题**：MinGW32 编译器无法处理含中文的路径（如 `C:\Projects\aardio\看穿gui`），
CMake 配置和 make 编译都会失败。

**解决方案**：将源码复制到纯 ASCII 路径下编译。

## 编译步骤

### 1. 准备源码副本

将 `libcimbar-0.6.5` 复制到纯英文路径：

```bash
# 在 MSYS2 mingw32 shell 中
cp -r /c/Projects/aardio/看穿gui/libcimbar-0.6.5 /c/Projects/lc-src
```

如果修改了 `cimbar_dll.cpp`，只需覆盖该文件：

```bash
cp /c/Projects/aardio/看穿gui/libcimbar-0.6.5/src/lib/cimbar_dll/cimbar_dll.cpp \
   /c/Projects/lc-src/src/lib/cimbar_dll/cimbar_dll.cpp
```

### 2. 创建构建目录并配置 CMake

```bash
mkdir -p /c/Projects/lc-build
cd /c/Projects/lc-build

cmake /c/Projects/lc-src \
  -G "MinGW Makefiles" \
  -DCMAKE_C_COMPILER=C:/Tools/msys64/mingw32/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/Tools/msys64/mingw32/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Tools/msys64/mingw32/bin/mingw32-make.exe \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_DLL=ON \
  -DOPENCV_DIR=C:/Projects/opencv-dist \
  -DOPENCV_STATIC=ON
```

**参数说明**：

| 参数 | 说明 |
|------|------|
| `-G "MinGW Makefiles"` | 使用 MinGW Makefile 生成器 |
| `-DCMAKE_C/CXX_COMPILER` | 指定 32 位 MinGW 编译器 |
| `-DBUILD_DLL=ON` | 启用 cimbar_dll 目标（否则不会编译 DLL） |
| `-DOPENCV_DIR` | OpenCV 安装根目录 |
| `-DOPENCV_STATIC=ON` | 静态链接 OpenCV（绿色部署，无需随 DLL 发布 OpenCV） |

### 3. 编译

```bash
cd /c/Projects/lc-build
mingw32-make cimbar_dll -j4
```

编译产物位于：
```
C:\Projects\lc-build\build\src\lib\cimbar_dll\libcimbar_dll.dll
```

### 4. 部署

将 DLL 复制到项目目录（需通过 MSYS2 bash 操作，因目标路径含中文）：

```bash
cp /c/Projects/lc-build/build/src/lib/cimbar_dll/libcimbar_dll.dll \
   "/c/Projects/aardio/看穿gui/dll/cimbar_dll.dll"
```

随 EXE 发布时需包含以下文件：

| 文件 | 说明 |
|------|------|
| `dll/cimbar_dll.dll` | 主 DLL（已静态链接 OpenCV、zstd、wirehair 等） |
| `dll/libwinpthread-1.dll` | MinGW32 POSIX 线程运行时 |

### 5. 增量编译

修改 `cimbar_dll.cpp` 后，只需：

```bash
# 1. 复制修改后的源文件
cp /c/Projects/aardio/看穿gui/libcimbar-0.6.5/src/lib/cimbar_dll/cimbar_dll.cpp \
   /c/Projects/lc-src/src/lib/cimbar_dll/cimbar_dll.cpp

# 2. 重新编译（仅重编译变更的 .cpp，其余目标使用缓存）
cd /c/Projects/lc-build
mingw32-make cimbar_dll -j4

# 3. 复制到项目
cp /c/Projects/lc-build/build/src/lib/cimbar_dll/libcimbar_dll.dll \
   "/c/Projects/aardio/看穿gui/dll/cimbar_dll.dll"
```

如果 CMakeLists.txt 有变更，需要重新配置（回到步骤 2）。

### 6. 清理编译目录

编译完成后，如需释放磁盘空间：

```bash
rm -rf /c/Projects/lc-src /c/Projects/lc-build
```

下次编译时从步骤 1 重新开始即可。

## 一键编译脚本

可在 MSYS2 mingw32 shell 中执行以下命令完成增量编译：

```bash
#!/bin/bash
# rebuild_dll.sh - 增量编译 cimbar_dll.dll
set -e

SRC_DIR=/c/Projects/lc-src
BUILD_DIR=/c/Projects/lc-build
PROJ_SRC=/c/Projects/aardio/看穿gui/libcimbar-0.6.5
PROJ_DLL="/c/Projects/aardio/看穿gui/dll/cimbar_dll.dll"

# 首次编译：创建源码副本和构建目录
if [ ! -d "$SRC_DIR" ]; then
    echo "=== 首次编译：复制源码 ==="
    cp -r "$PROJ_SRC" "$SRC_DIR"
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "=== 首次编译：CMake 配置 ==="
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$SRC_DIR" \
      -G "MinGW Makefiles" \
      -DCMAKE_C_COMPILER=C:/Tools/msys64/mingw32/bin/gcc.exe \
      -DCMAKE_CXX_COMPILER=C:/Tools/msys64/mingw32/bin/g++.exe \
      -DCMAKE_MAKE_PROGRAM=C:/Tools/msys64/mingw32/bin/mingw32-make.exe \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_DLL=ON \
      -DOPENCV_DIR=C:/Projects/opencv-dist \
      -DOPENCV_STATIC=ON
fi

# 同步修改后的源文件
echo "=== 同步 cimbar_dll.cpp ==="
cp "$PROJ_SRC/src/lib/cimbar_dll/cimbar_dll.cpp" \
   "$SRC_DIR/src/lib/cimbar_dll/cimbar_dll.cpp"

# 编译
echo "=== 编译 ==="
cd "$BUILD_DIR"
mingw32-make cimbar_dll -j4

# 部署
echo "=== 部署 ==="
cp "$BUILD_DIR/build/src/lib/cimbar_dll/libcimbar_dll.dll" "$PROJ_DLL"

echo "=== 完成 ==="
```

## 常见问题

### Q: CMake 配置失败 "Could not find OpenCV"
确保 `-DOPENCV_DIR` 指向正确的 OpenCV 安装目录，且该目录下有 `x64/mingw/staticlib/` 子目录。

### Q: 编译失败 "No rule to make target"
中文路径问题。确保源码目录和构建目录都在纯 ASCII 路径下。

### Q: 运行时 SIGILL (非法指令)
wirehair 库默认使用 `-march=native` 编译，会生成当前 CPU 特有指令（如 AVX2）。
如果目标机器 CPU 不支持这些指令，会触发 SIGILL 崩溃。
解决方案：在 CMake 配置时添加 `-DCMAKE_CXX_FLAGS=-march=i686` 覆盖 `-march=native`。

### Q: DLL 体积过大
当前 DLL 约 20MB（含静态链接的 OpenCV、zstd、wirehair 等），属于正常范围。
如需减小体积，可尝试：
- 使用 `-DCMAKE_BUILD_TYPE=MinSizeRel` 优化体积
- 裁剪 OpenCV 模块（只保留必需的 core/imgproc/imgcodecs/calib3d）

### Q: 从 PowerShell 调用 MSYS2 编译
PowerShell 中调用 MSYS2 shell 执行命令：

```powershell
# 方式 1：使用 msys2_shell.cmd（推荐）
C:\Tools\msys64\msys2_shell.cmd -mingw32 -defterm -no-start -c "cd /c/Projects/lc-build && mingw32-make cimbar_dll -j4 2>&1"

# 方式 2：使用 mingw32.exe
C:\Tools\msys64\mingw32.exe -c "cd /c/Projects/lc-build && mingw32-make cimbar_dll -j4 2>&1"
```

注意：PowerShell 中 MSYS2 的 stdout 可能不显示，但退出码可用。编译是否成功需检查 DLL 文件的时间戳。

## DLL 导出函数一览

详见 [cimbar_dll.h](libcimbar-0.6.5/src/lib/cimbar_dll/cimbar_dll.h)。

| 分类 | 函数 | 说明 |
|------|------|------|
| 编码 | `cimbar_encode_file` | 从文件初始化编码 |
| | `cimbar_encode_buffer` | 从内存初始化编码 |
| | `cimbar_encode_next_frame` | 生成下一帧 |
| | `cimbar_encode_create_hbitmap` | 获取当前帧 HBITMAP |
| | `cimbar_encode_get_info` | 获取编码进度信息 |
| | `cimbar_encode_cleanup` | 清理编码状态 |
| 解码 | `cimbar_decode_configure` | 配置解码模式 |
| | `cimbar_decode_scan_extract` | 扫描提取帧数据 |
| | `cimbar_decode_fountain` | 喷泉码解码 |
| | `cimbar_decode_decompress_read` | 解压并读取文件 |
| | `cimbar_decode_image` | 一步解码图像文件 |
| | `cimbar_decode_cleanup` | 清理解码状态 |
| 摄像头 | `cimbar_cam_open` | 打开摄像头 (Media Foundation) |
| | `cimbar_cam_grab_hbitmap` | 抓帧返回 HBITMAP |
| | `cimbar_cam_close` | 关闭摄像头 |
