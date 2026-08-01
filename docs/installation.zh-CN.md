# 安装指南

## 前置依赖

- C17 编译器（GCC >= 8 或 Clang >= 10）
- CMake >= 3.14
- libcurl 开发头文件
- pkg-config
- make（或 ninja）

## 安装依赖

### Debian / Ubuntu
```bash
sudo apt install cmake gcc libcurl4-openssl-dev pkg-config
```

### RHEL / Fedora / CentOS
```bash
sudo dnf install cmake gcc libcurl-devel pkgconfig
```

### Arch Linux
```bash
sudo pacman -S cmake gcc curl pkg-config
```

### macOS
```bash
# Homebrew
brew install cmake gcc curl pkg-config

# 或 Xcode Command Line Tools
xcode-select --install
```

### Alpine Linux
```bash
apk add cmake gcc musl-dev libcurl-dev pkgconf
```

### 国内网络

如果下载依赖慢，可配置代理后执行：
```bash
export HTTP_PROXY=http://127.0.0.1:7890
export HTTPS_PROXY=$HTTP_PROXY
```

## 编译

```bash
git clone https://github.com/user/gitsight.git
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
