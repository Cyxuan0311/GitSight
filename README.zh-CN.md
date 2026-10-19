# GitSight

<p align="center">
  <img src="https://img.shields.io/badge/C17-00599C?style=for-the-badge&logo=c&logoColor=white" alt="C17">
  <img src="https://img.shields.io/badge/%E5%8D%8F%E8%AE%AE-MIT-brightgreen?style=for-the-badge" alt="MIT">
  <img src="https://img.shields.io/badge/%E5%B9%B3%E5%8F%B0-Linux%20|%20macOS-316192?style=for-the-badge" alt="Platform">
  <img src="https://img.shields.io/badge/%E6%9E%84%E5%BB%BA-%E9%80%9A%E8%BF%87-brightgreen?style=for-the-badge" alt="Build">
  <img src="https://img.shields.io/badge/%E8%BE%93%E5%87%BA-JSON%20|%20MD%20|%20TXT%20|%20XLSX%20|%20PDF-important?style=for-the-badge" alt="Formats">
  <img src="https://img.shields.io/badge/PR-%E6%AC%A2%E8%BF%8E-ff69b4?style=for-the-badge" alt="PRs Welcome">
</p>

分析任意公开 GitHub 仓库的 Pull Request、Commit、Issue，以 7 种格式导出结构化报告，并可选 AI 智能摘要。
`gs serve` 可在同一套引擎之上启动本地 Web 控制台。

## 特性

- **多源分析** — 支持 GitHub、GitLab、Codeberg、Gitea、Forgejo
- **Web 控制台** — `gs serve` 把同一套引擎搬到浏览器，实时进度 + 秒切导出格式
- **LLM 智能摘要** — 接入任何 OpenAI 兼容 API（OpenAI、Ollama、OpenRouter 等）
- **7 种导出格式** — JSON、Markdown、纯文本、HTML、CSV、XLSX、PDF（支持单次多格式导出）
- **高度可配置** — 板块可见性、字体、颜色、列宽、页面布局
- **零运行时依赖** — PDF 和 XLSX 原生生成，无需额外工具
- **网络友好** — HTTP 代理、API 镜像、GitHub 企业版支持
- **快速、可移植的 C17** — 单二进制文件，无需解释器或虚拟机

## 目录

- [安装](#安装)
- [快速开始](#快速开始)
- [使用指南](#使用指南)
- [Web 控制台](#web-控制台)
- [导出格式](#导出格式)
- [配置](#配置)
- [文档](#文档)
- [贡献](#贡献)
- [许可协议](#许可协议)

## 安装

### 依赖

- C17 编译器（GCC >= 8 / Clang >= 10）
- CMake >= 3.14
- libcurl + pkg-config

### 安装构建依赖

| 发行版 | 命令 |
|--------|------|
| Debian / Ubuntu | `sudo apt install cmake gcc libcurl4-openssl-dev pkg-config` |
| Fedora / RHEL | `sudo dnf install cmake gcc libcurl-devel pkgconfig` |
| Arch Linux | `sudo pacman -S cmake gcc curl pkg-config` |
| macOS（Homebrew） | `brew install cmake gcc curl pkg-config` |
| Alpine | `apk add cmake gcc musl-dev libcurl-dev pkgconf` |

### 编译安装

```bash
git clone https://github.com/user/gitsight.git
cd gitsight
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install          # → /usr/local/bin/gs

# 验证
gs version
```

## 快速开始

```bash
# 分析公开仓库（默认 Markdown 输出到 stdout）
gs analyze curl/curl

# 保存为 JSON
gs analyze rust-lang/rust -f json -o report.json

# 指定时间范围
gs analyze torvalds/linux --since 30

# XLSX / PDF 输出（需要 -o）
gs analyze owner/repo -f xlsx -o report.xlsx
gs analyze owner/repo -f pdf -o report.pdf

# 同时导出多种格式
gs analyze owner/repo -f json,md,pdf -o ./report

# 启用 AI 分析（需要配置 LLM）
gs analyze owner/repo
```

## 使用指南

### 认证

```bash
export GITHUB_TOKEN=ghp_xxxxxxxxxxxx
# 或持久化保存
gs config --set-token ghp_xxxxxxxxxxxx
```

### 仓库格式

```
gs analyze curl/curl
gs analyze https://github.com/curl/curl
gs analyze https://github.com/curl/curl.git
gs analyze git@github.com:curl/curl.git
```

### 颜色控制

```bash
gs analyze owner/repo --color always      # 强制彩色
gs analyze owner/repo --color never       # 无颜色
gs analyze owner/repo --color auto         # 默认（终端时启用）
```

### 代理与镜像

```bash
gs analyze owner/repo --proxy http://127.0.0.1:7890
gs analyze owner/repo --api-base https://api.github.com
```

## Web 控制台

```bash
gs serve                  # → http://127.0.0.1:8787
```

`gs serve` 把同一套分析、导出与配置引擎放到浏览器界面之后：九个页面覆盖
分析、对比、批量、订阅、用量以及全部 `gs config --set` 配置项。进度通过
SSE 实时推送到浏览器，运行中的任务可随时取消；分析结果保留在内存中，因此
切换导出格式只是重新渲染，不会重新抓取。

`gs serve` 默认只监听回环地址且**没有鉴权**——它会读写保存访问令牌的配置
文件。只有在你信任的网络里才使用 `--host 0.0.0.0`。

页面清单、构建步骤与控制台开发环境见
[web/README.zh-CN.md](web/README.zh-CN.md)。HTTP 接口文档见
[docs/usage.zh-CN.md](docs/usage.zh-CN.md#http-接口)。

## 导出格式

| 格式 | 扩展名 | 输出方式 | 可配置项 |
|------|--------|---------|---------|
| **JSON** | `.json` | stdout / 文件 | — |
| **Markdown** | `.md` | stdout / 文件 | 表格宽度、板块可见性 |
| **纯文本** | `.txt` | stdout / 文件 | 表格宽度、板块可见性 |
| **HTML** | `.html` | stdout / 文件 | 自包含单页：内嵌 CSS、状态徽章、链接、目录锚点、活动图表 |
| **CSV** | `.csv` | stdout / 文件 | 分块规整数据表（metrics / pull_requests / commits / issues / releases） |
| **XLSX** | `.xlsx` | 仅文件 | 字体、列宽、边框、工作表名 |
| **PDF** | `.pdf` | 仅文件 | 边距、行高、颜色、页面尺寸 |

### 自定义导出输出

```bash
# 隐藏某个板块
gs analyze owner/repo --export show_commits=false

# PDF 布局与颜色
gs analyze owner/repo -f pdf -o report.pdf \
  --color always \
  --export pdf.margin=40 \
  --export pdf.title_color=200,50,50 \
  --export pdf.line_height=1.6

# XLSX 格式化
gs analyze owner/repo -f xlsx -o report.xlsx \
  --export xlsx.font_size=14 \
  --export xlsx.border=false \
  --export xlsx.sheet_name="汇总"
```

完整的配置项见[导出配置](docs/configuration.zh-CN.md)。

## 配置

配置文件位于 `~/.config/gitsight/config.json`（首次运行 `gs config` 时自动创建）。

### LLM 提供商

```json
{
  "llms": {
    "openai": {
      "api_key": "sk-xxx",
      "api_base": "https://api.openai.com/v1",
      "model": "gpt-4o-mini"
    },
    "local": {
      "api_key": "not-needed",
      "api_base": "http://localhost:11434/v1",
      "model": "llama3.2"
    }
  }
}
```

切换提供商：

```bash
gs analyze owner/repo --llm local
gs config --set-llm local          # 持久化
```

### 环境变量

| 变量 | 覆盖项 |
|------|--------|
| `GITHUB_TOKEN` | GitHub Token |
| `LLM_API_KEY` | 当前 LLM 密钥 |
| `HTTPS_PROXY` | 代理地址 |
| `NO_COLOR` | 禁用颜色 |

## Shell 补全

bash、zsh、fish 的 Tab 补全已内嵌在 `gs` 二进制中。用 `gs completion <shell>`
输出脚本并安装:

```bash
# bash
gs completion bash > ~/.config/gitsight/gs.bash
echo "source ~/.config/gitsight/gs.bash" >> ~/.bashrc

# zsh
gs completion zsh > ~/.config/gitsight/_gs
echo "fpath=($HOME/.config/gitsight \$fpath)" >> ~/.zshrc
exec zsh

# fish
gs completion fish > ~/.config/fish/completions/gs.fish
```

补全会读取你的配置:仓库名来自 `~/.config/gitsight/config.json` 的
`subscriptions`(或 `--config <path>` 指定),LLM 名来自已配置的提供商。

## 文档

| 指南 | 说明 |
|------|------|
| [安装](docs/installation.zh-CN.md) | 各平台编译方法 |
| [使用](docs/usage.zh-CN.md) | 完整命令参考与示例 |
| [配置](docs/configuration.zh-CN.md) | LLM、代理、导出配置 |
| [架构](docs/architecture.zh-CN.md) | 代码结构与扩展指南 |

## 贡献

欢迎提交 PR！代码结构概览见[架构文档](docs/architecture.zh-CN.md)。

- 问题追踪：[GitHub Issues](https://github.com/user/gitsight/issues)
- 提交大型功能 PR 前请先提 Issue 讨论

## 许可协议

MIT
