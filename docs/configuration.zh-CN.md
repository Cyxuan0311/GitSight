# 配置说明

## 配置文件位置

`~/.config/gitsight/config.json`（JSON 格式）

首次运行 `gs config` 时自动创建默认配置。

## 配置优先级

1. CLI 命令行参数（最高）
2. 配置文件
3. 环境变量
4. 内置默认值（最低）

## 完整字段

```json
{
    "github_token": "ghp_xxxxxxxxxxxx",
    "api_base": "https://api.github.com",
    "proxy": "http://127.0.0.1:7890",
    "default_days": 7,
    "color": "auto",
    "format": "md",
    "active_llm": "openai",
    "llms": {
        "openai": {
            "api_key": "sk-xxxxxxxxxxxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "system_prompt": "You are a code analyst..."
        },
        "local": {
            "api_key": "not-needed",
            "api_base": "http://localhost:11434/v1",
            "model": "llama3.2",
            "system_prompt_file": "my_prompt.md"
        }
    },
    "export": {
        "show_prs": true,
        "show_commits": true,
        "show_issues": true,
        "show_summary": true,
        "show_top_labels": true,
        "md": { "table_width": 60 },
        "txt": { "table_width": 56 },
        "xlsx": {
            "font_size": 11,
            "font_name": "Calibri",
            "col_a_width": 20,
            "col_b_width": 60,
            "border": true,
            "sheet_name": "Report"
        },
        "pdf": {
            "margin": 50,
            "line_height": 1.4,
            "col2_padding": 12,
            "title_color": "25,55,160",
            "header_color": "25,55,160",
            "label_color": "90,90,90"
        }
    }
}
```

## 字段说明

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `github_token` | string | `""` | GitHub 个人访问令牌 |
| `api_base` | string | `"https://api.github.com"` | GitHub API 地址（镜像/企业版） |
| `proxy` | string | `""` | HTTP/HTTPS/SOCKS5 代理地址 |
| `default_days` | number | `7` | 默认分析天数 |
| `color` | string | `"auto"` | 颜色模式：`"auto"`, `"always"`, `"never"` |
| `format` | string | `"md"` | 默认输出格式：`"md"`, `"json"`, `"txt"`, `"xlsx"`, `"pdf"` |
| `active_llm` | string | `""` | 当前激活的 LLM 提供商名称 |
| `llms.<name>.api_key` | string | `""` | LLM API 密钥 |
| `llms.<name>.api_base` | string | `"https://api.openai.com/v1"` | LLM API 地址 |
| `llms.<name>.model` | string | `"gpt-4o-mini"` | LLM 模型名 |
| `llms.<name>.system_prompt` | string | `""` | 自定义系统提示词（内联，覆盖内置默认值） |
| `llms.<name>.system_prompt_file` | string | `""` | 提示词文件名（在 `prompts/` 目录下，优先级高于 `system_prompt`） |
| `export.show_prs` | bool | `true` | 是否显示 Pull Requests 板块 |
| `export.show_commits` | bool | `true` | 是否显示 Commits 板块 |
| `export.show_issues` | bool | `true` | 是否显示 Issues 板块 |
| `export.show_summary` | bool | `true` | 是否显示 Summary 板块 |
| `export.show_top_labels` | bool | `true` | 是否显示标签板块 |
| `export.md.table_width` | number | `60` | Markdown 表格宽度 |
| `export.txt.table_width` | number | `56` | 纯文本表格宽度（字符数） |
| `export.xlsx.font_size` | number | `11` | XLSX 字号（pt） |
| `export.xlsx.font_name` | string | `"Calibri"` | XLSX 字体名称 |
| `export.xlsx.col_a_width` | number | `20` | XLSX A 列宽度 |
| `export.xlsx.col_b_width` | number | `60` | XLSX B 列宽度 |
| `export.xlsx.border` | bool | `true` | 是否显示 XLSX 单元格边框 |
| `export.xlsx.sheet_name` | string | `"Report"` | XLSX 工作表名称 |
| `export.pdf.margin` | number | `50` | PDF 页面边距（pt） |
| `export.pdf.line_height` | number | `1.4` | PDF 行高倍率（× 字号） |
| `export.pdf.col2_padding` | number | `12` | PDF 第二列偏移间距（pt） |
| `export.pdf.title_color` | string | `"25,55,160"` | PDF 标题颜色（R,G,B） |
| `export.pdf.header_color` | string | `"25,55,160"` | PDF 板块标题颜色（R,G,B） |
| `export.pdf.label_color` | string | `"90,90,90"` | PDF 指标标签颜色（R,G,B） |

## CLI 覆盖

导出配置可以通过 `--export` 参数在运行时覆盖：

```bash
# 隐藏某个板块
gs analyze owner/repo --export show_commits=false

# 自定义 PDF 布局
gs analyze owner/repo --export pdf.margin=40 --export pdf.line_height=1.6

# 自定义 PDF 颜色
gs analyze owner/repo --color always --export pdf.title_color=200,50,50

# 自定义 XLSX
gs analyze owner/repo --export xlsx.font_size=14 --export xlsx.sheet_name="我的报表"

# 组合多个覆盖
gs analyze owner/repo \
  --export show_issues=false \
  --export pdf.margin=30 \
  --export pdf.title_color=0,0,200
```

注意：PDF 颜色仅在使用 `--color always` 输出到文件时生效。

## 环境变量

| 变量 | 覆盖的配置字段 |
|------|---------------|
| `GITHUB_TOKEN` | `github_token` |
| `GS_API_BASE` | `api_base` |
| `HTTP_PROXY` | `proxy` |
| `HTTPS_PROXY` | `proxy` |
| `NO_COLOR` | `color`（设置后禁用颜色） |
| `LLM_API_KEY` | `active_llm.api_key` |
| `LLM_API_BASE` | `active_llm.api_base` |
| `LLM_MODEL` | `active_llm.model` |

## 多 LLM 提供商

配置文件支持任意数量的命名 LLM 提供商。通过 `gs config --set-llm <name>` 或
`gs analyze <repo> --llm <name>` 切换。

示例（三个提供商）：

```json
{
    "active_llm": "openai",
    "llms": {
        "openai": {
            "api_key": "sk-xxxxxxxxxxxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini"
        },
        "local-llama": {
            "api_key": "not-needed",
            "api_base": "http://localhost:8080/v1",
            "model": "local-model"
        },
        "local-ollama": {
            "api_key": "ollama",
            "api_base": "http://localhost:11434/v1",
            "model": "llama3.2"
        }
    }
}
```

## 自定义系统提示词

可以通过系统提示词自定义 LLM 的分析方式。

### 内联提示词

```json
{
    "llms": {
        "openai": {
            "api_key": "sk-xxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "system_prompt": "你是一名高级代码审查员，专注于安全问题和潜在缺陷。"
        }
    }
}
```

### 提示词文件

在 `~/.config/gitsight/prompts/` 目录下放置提示词文件，通过文件名引用：

```
~/.config/gitsight/prompts/my_reviewer.md
```

```json
{
    "llms": {
        "openai": {
            "api_key": "sk-xxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "system_prompt_file": "my_reviewer.md"
        }
    }
}
```

**优先级**：`system_prompt_file` > `system_prompt` > 内置默认提示词。

## 通过 CLI 管理配置

```bash
# 查看配置路径
gs config

# 保存 GitHub Token
gs config --set-token ghp_xxxxxxxxxxxx

# 列出可用 LLM 提供商
gs config --list-llms

# 设置当前激活的 LLM
gs config --set-llm local-ollama

# 单次运行切换 LLM
gs analyze owner/repo --llm openai

# 查看当前配置
cat ~/.config/gitsight/config.json
```
