# 使用指南

## 命令结构

```bash
gs <command> [options]
gs analyze <owner/repo> [options]
gs config [options]
gs version
```

## Analyze 命令

### 基本用法

```bash
# 分析最近 7 天，Markdown 输出到 stdout
gs analyze rust-lang/rust

# 分析最近 30 天
gs analyze rust-lang/rust --since 30

# JSON 输出
gs analyze owner/repo --format json

# 纯文本输出
gs analyze owner/repo --format txt
```

### 仓库格式

支持多种格式传入 `owner/repo`：

```bash
# 简洁格式
gs analyze curl/curl

# 完整 URL
gs analyze https://github.com/curl/curl

# 带 .git 后缀
gs analyze https://github.com/curl/curl.git

# SSH 格式
gs analyze git@github.com:curl/curl.git
```

### 输出选项

```bash
# 保存到文件
gs analyze owner/repo -f md -o report.md

# 同时输出多种格式
gs analyze owner/repo -f json,md,txt -o ./report

# XLSX / PDF（需要 -o，不能输出到 stdout）
gs analyze owner/repo -f xlsx -o report.xlsx
gs analyze owner/repo -f pdf -o report.pdf

# 混合二进制和文本格式
gs analyze owner/repo -f xlsx,md -o ./report
gs analyze owner/repo -f pdf,json -o ./report

# 管道到其他工具（如 glow 渲染 markdown）
gs analyze owner/repo | glow -
gs analyze owner/repo --format json | jq .
```

管道传输时，进度信息会输出到 stderr，不会干扰管道数据。

### 网络配置

```bash
# HTTP 代理（国内网络）
gs analyze owner/repo --proxy http://127.0.0.1:7890

# 自定义 API 地址（GitHub 企业版或镜像）
gs analyze owner/repo --api-base https://api.github.com

# 或通过环境变量
export HTTP_PROXY=http://127.0.0.1:7890
export GS_API_BASE=https://api.github.com
```

### 认证

```bash
# 命令行参数（单次）
gs analyze owner/repo --token ghp_xxxxxxxxxxxx

# 环境变量（会话有效）
export GITHUB_TOKEN=ghp_xxxxxxxxxxxx

# 持久化到配置文件
gs config --set-token ghp_xxxxxxxxxxxx
```

优先级：`--token` 参数 > 配置文件 > `GITHUB_TOKEN` 环境变量。

### 颜色控制

```bash
gs analyze owner/repo --color always    # 强制彩色
gs analyze owner/repo --color never     # 禁用彩色
gs analyze owner/repo --color auto      # 自动（默认）
export NO_COLOR=1                       # 环境变量禁用
```

### LLM 智能分析

```bash
# 使用配置文件中激活的 LLM 提供商
gs analyze owner/repo

# 单次运行切换提供商
gs analyze owner/repo --llm openai

# 通过环境变量（会创建/覆盖激活的提供商）
export LLM_API_KEY=sk-xxx
export LLM_API_BASE=https://api.openai.com/v1
export LLM_MODEL=gpt-4o-mini
gs analyze owner/repo
```

详细的多提供商配置和自定义提示词见 [Configuration](configuration.md)。

## Config 命令

```bash
# 查看配置路径
gs config

# 持久化保存 GitHub Token
gs config --set-token ghp_xxxxxxxxxxxx

# 列出可用 LLM 提供商
gs config --list-llms

# 设置当前激活的 LLM（持久化）
gs config --set-llm local-ollama
```

### 导出配置

```bash
# 隐藏某个板块
gs analyze owner/repo --export show_commits=false

# 自定义 PDF 布局
gs analyze owner/repo -f pdf -o report.pdf --export pdf.margin=40

# 自定义 PDF 颜色（需要 --color always）
gs analyze owner/repo -f pdf -o report.pdf --color always --export pdf.title_color=200,50,50

# 自定义 XLSX
gs analyze owner/repo -f xlsx -o report.xlsx --export xlsx.font_size=14 --export xlsx.border=false

# 与其他选项组合使用
gs analyze owner/repo -f pdf,json -o ./report --export show_issues=false --export pdf.margin=30
```

详细配置项见 [Configuration](configuration.md)。

### 自定义提示词目录

在 `~/.config/gitsight/prompts/` 目录下放置提示词文件：

```
~/.config/gitsight/prompts/
  security_review.md
  changelog_gen.md
```

在 LLM 提供商的 `system_prompt_file` 字段中引用。
详情见 [Configuration](configuration.md)。

## Version 命令

```bash
gs version
# GitSight version 0.1.0
```

## 输出示例

### Markdown

```
# GitSight Report

**Repository**: curl/curl
**Period**:     last 7 days

## [PR] Pull Requests

| Metric | Value |
|--------|-------|
| Total  | 42 |
| Merged | 35 |

...
```

### JSON

```json
{
    "owner": "curl",
    "repo": "curl",
    "period": "last 7 days",
    "pull_requests": {
        "total": 42, "merged": 35,
        "open": 5, "closed": 2
    }
}
```

### 纯文本

```
+------------------------------------------------------+
|                    GitSight Report                    |
+------------------------------------------------------+
| Repository:  curl/curl                                |
| Period:      last 7 days                              |
...
+------------------------------------------------------+
```
