# Installation

## Prerequisites

- C17 compiler (GCC >= 8 or Clang >= 10)
- CMake >= 3.14
- libcurl development headers
- zlib development headers (used by XLSX/PDF export)
- make (or ninja)

> cJSON and miniz are [vendored](../third_party) and compiled into the binary,
> so no extra runtime libraries are needed to produce JSON, Markdown, XLSX or
> PDF exports.

## Install Dependencies

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

# Or Xcode Command Line Tools (curl & zlib ship with macOS)
xcode-select --install
```

### Alpine Linux
```bash
apk add cmake gcc musl-dev libcurl-dev zlib-dev
```

## Build

```bash
git clone https://github.com/Cyxuan0311/GitSight.git
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
