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
    "platform": "github",
    "lang": "en",
    "github_token": "ghp_xxxxxxxxxxxx",
    "gitlab_token": "glpat_xxxxxxxxxxxx",
    "codeberg_token": "xxxxxxxxxxxxxxxxxxxx",
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
            "label_color": "90,90,90",
            "link_color": "17,85,204",
            "open_color": "26,127,55",
            "closed_color": "207,34,46",
            "merged_color": "130,80,223"
        }
    }
}
```

## 字段说明

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `platform` | string | `"github"` | 平台：`"github"`, `"gitlab"`, `"codeberg"`, `"gitea"`, `"forgejo"` |
| `lang` | string | `"en"` | 报告正文语言：`"en"` 或 `"zh"`（影响 md/txt/pdf 报告与 LLM 提示词；JSON/XLSX 保持英文）|
| `github_token` | string | `""` | GitHub 个人访问令牌 |
| `gitlab_token` | string | `""` | GitLab 个人访问令牌 |
| `codeberg_token` | string | `""` | Gitea 系平台（Codeberg/Gitea/Forgejo）的令牌 |
| `api_base` | string | 平台默认值 | API 地址（各平台有默认值；镜像/自建时覆盖） |
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
| `export.pdf.link_color` | string | `"17,85,204"` | PDF 可点击条目（PR/commit/issue/release 标题）颜色（R,G,B） |
| `export.pdf.open_color` | string | `"26,127,55"` | PDF `[open]` 状态标签与 Open 计数颜色（R,G,B） |
| `export.pdf.closed_color` | string | `"207,34,46"` | PDF `[closed]` 状态标签与 Closed 计数颜色（R,G,B） |
| `export.pdf.merged_color` | string | `"130,80,223"` | PDF `[merged]` 状态标签与 Merged 计数颜色（R,G,B） |

## 平台

`platform` 用于选择 API 提供商。GitHub 与 GitLab 使用各自的驱动；Codeberg、
Gitea、Forgejo 共用一套"兼容 GitHub 的驱动"（REST 路径相同，仅认证头与默认
API 地址不同）。

| 平台 | 默认 `api_base` | 认证头 |
|------|-----------------|--------|
| `github`   | `https://api.github.com`       | `Authorization: Bearer <token>` |
| `gitlab`   | `https://gitlab.com/api/v4`    | `PRIVATE-TOKEN: <token>` |
| `codeberg` | `https://codeberg.org/api/v1`  | `Authorization: token <token>` |
| `gitea`    | `https://gitea.com/api/v1`     | `Authorization: token <token>` |
| `forgejo`  | `https://codeberg.org/api/v1`  | `Authorization: token <token>` |

分析 URL 时会根据主机名自动识别平台（`gitlab.com` → GitLab，`codeberg.org` →
Codeberg，包含 `gitea` 或 `forgejo` 的主机 → Gitea）并应用对应的默认
`api_base`。可用 `--platform` 或配置中的 `platform` 字段覆盖，自建实例用
`--api-base` / `api_base` 指定：

```bash
# Codeberg（从 URL 自动识别）
gs analyze https://codeberg.org/acme/gadget

# 自建 Gitea
gs analyze acme/gadget --platform gitea --api-base https://gitea.example.com/api/v1
```

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

PDF 导出包含中文时会自动内嵌系统中可找到的 TrueType 中文字体子集（依次查找 `GITSIGHT_PDF_FONT` 环境变量、`~/.config/gitsight/fonts/cjk.ttf`、Windows 微软雅黑/黑体/宋体、文泉驿、Arphic 等）。可用 `GITSIGHT_PDF_FONT=/path/to/font.ttf` 指定字体。

## 分支分析

默认情况下，commits 从仓库的默认分支获取。使用 `--branch` 分析特定分支：

```bash
# 分析 develop 分支
gs analyze owner/repo --branch develop

# 与其他选项组合使用
gs analyze owner/repo --branch feature/auth -d 30 -f json -o report.json
```

`--branch` 参数仅影响 commits 获取。PR、Issues、Releases 和 Contributors 始终是仓库级别的，不受分支选择影响。

与 `gs watch` 配合使用时，分支会保存在订阅中，并在每次批量更新时应用：

```bash
gs watch add owner/repo --branch develop
gs watch update owner/repo --branch main
```

## 批量并行

一次分析多个仓库时（`analyze` 传多个参数，或 `watch` 有多个订阅），默认逐个
串行执行。使用 `-j` / `--parallel` 可让多个仓库并发分析：

```bash
# 分析 4 个仓库，最多同时 4 个
gs analyze a/b c/d e/f g/h -j 4

# watch 批量同样支持
gs watch -j 8
```

- 上限为 8。
- 未配置 API token 时默认值为 `1`（GitHub 未认证请求每小时仅约 60 次，并发会
  触发 HTTP 403）；配置 token 后默认为 `4`。
- 无 token 时显式传入 `-j N` 仍会执行，但会打印速率限制警告。

## 环境变量

| 变量 | 覆盖的配置字段 |
|------|---------------|
| `GITHUB_TOKEN` | `github_token` |
| `GITLAB_TOKEN` | `gitlab_token` |
| `CODEBERG_TOKEN` / `GS_CODEBERG_TOKEN` | `codeberg_token` |
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
