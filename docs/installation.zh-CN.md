# 安装指南

## 前置依赖

- C17 编译器（GCC >= 8 或 Clang >= 10）
- CMake >= 3.14
- libcurl 开发头文件
- zlib 开发头文件（XLSX/PDF 导出使用）
- make（或 ninja）

> cJSON 与 miniz 已 [内嵌](../third_party) 进二进制，因此生成 JSON、Markdown、XLSX 或 PDF 报告无需额外运行时库。

## 安装依赖

### Debian / Ubuntu
```bash
sudo apt install cmake gcc libcurl4-openssl-dev zlib1g-dev
```

### RHEL / Fedora / CentOS
```bash
sudo dnf install cmake gcc libcurl-devel zlib-devel
```

### Arch Linux
```bash
sudo pacman -S cmake gcc curl zlib
```

### macOS
```bash
# Homebrew
brew install cmake curl zlib

# 或 Xcode Command Line Tools（macOS 自带 curl 与 zlib）
xcode-select --install
```

### Alpine Linux
```bash
apk add cmake gcc musl-dev libcurl-dev zlib-dev
```

### 国内网络

如果下载依赖慢，可配置代理后执行：
```bash
export HTTP_PROXY=http://127.0.0.1:7890
export HTTPS_PROXY=$HTTP_PROXY
```

## 编译

```bash
git clone https://github.com/Cyxuan0311/GitSight.git
cd gitsight

# 使用构建脚本
chmod +x scripts/build.sh
./scripts/build.sh

# 或手动编译
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 安装

```bash
# 从 build 目录
sudo make install    # 安装到 /usr/local/bin/gs

# 或手动复制
sudo cp build/gs /usr/local/bin/gs
```

## 验证

```bash
gs version
# 输出: GitSight version 0.1.0
```
