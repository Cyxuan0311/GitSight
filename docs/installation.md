# Installation

## Prerequisites

- C17 compiler (GCC >= 8 or Clang >= 10)
- CMake >= 3.14
- libcurl development headers
- pkg-config
- make (or ninja)

## Install Dependencies

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

# Or Xcode Command Line Tools
xcode-select --install
```

### Alpine Linux
```bash
apk add cmake gcc musl-dev libcurl-dev pkgconf musl-dev
```

## Build

```bash
git clone https://github.com/user/gitsight.git
cd gitsight

# Using the build script
chmod +x scripts/build.sh
./scripts/build.sh

# Or manually
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Install

```bash
# From build directory
sudo make install    # installs to /usr/local/bin/gs

# Or copy manually
sudo cp build/gs /usr/local/bin/gs
```

## Static Build (no runtime dependencies)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON
make -j$(nproc)
```

## Verify

```bash
gs version
# Output: GitSight version 0.1.0
```
