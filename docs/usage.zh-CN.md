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

## Web 控制台

`gs serve` 启动一个本地 HTTP 守护进程，用浏览器界面覆盖上面的全部命令 ——
分析、对比、批量、订阅、LLM 提供商、设置、用量与补全脚本。

```bash
gs serve                      # → http://127.0.0.1:8787
gs serve --port 9000
gs serve --config ./scratch/config.json   # 使用独立的配置
```

| 选项 | 说明 |
|------|------|
| `--host <addr>` | 绑定地址，默认 `127.0.0.1`。 |
| `--port <n>` | 监听端口，默认 `8787`，也可用 `GS_PORT`。 |
| `--web-root <dir>` | 构建产物的目录，也可用 `GITSIGHT_WEB_ROOT`。 |
| `--config <path>` | 配置文件，默认 `~/.config/gitsight/config.json`。 |
| `--jobs <n>` | 并发分析任务数。 |
| `--quiet`、`-q` | 不打印启动横幅。 |

守护进程默认只监听回环地址且**没有鉴权** —— 它会读写保存访问令牌的配置
文件。只有在你信任的网络里才使用 `--host 0.0.0.0`。

### 构建控制台

```bash
npm --prefix web install
npm --prefix web run build          # → web/dist
cp -r web/dist build/web            # gs serve 默认按 ./web 查找

# 或交给 CMake 完成（需要 Node）：
cmake -B build -DGITSIGHT_BUILD_WEB=ON
cmake --build build
```

### 开发模式

```bash
gs serve --port 8787                # API 在一个端口
npm --prefix web run dev            # Vite 在 :5173，把 /api 代理到 :8787
```

若守护进程跑在别处，可用 `GS_API_TARGET` 覆盖代理目标。

### 相比 CLI 多出的能力

- **实时进度。** 分析以任务形式运行，阶段日志通过 SSE 推送到浏览器；运行中
  可随时取消。
- **切换格式无需重新抓取。** 任务完成后数据保留在内存中，任何导出格式都是
  重新渲染，因此瞬时完成。
- **跨仓库并行。** 批量任务最多同时分析 8 个仓库，每个仓库内部串行抓取。
- **可交互报告。** 报告在浏览器中原生渲染，支持筛选的表格与图表，布局与
  状态配色与 HTML 导出保持一致。

### 环境变量

| 变量 | 作用 |
|------|------|
| `GS_FIXTURE_DIR` | 用目录里的固定响应代替网络请求。守护进程同样有效，`tests/test_server.sh` 正是靠它离线跑通整个 API。 |
| `GS_CACHE_DIR` | 响应缓存的存放目录，默认 `$XDG_CACHE_HOME/gitsight`。指到别处即可在不打扰真实缓存的前提下测量或清理它。 |
| `GS_PERF` | 把一次运行的分阶段耗时（抓取、分析、导出）打印到 stderr。 |

```bash
GS_PERF=1 gs analyze owner/repo
```

```bash
GS_PERF=1 gs serve      # 每个任务各自打印自己的耗时
```

## HTTP 接口

守护进程暴露的接口与控制台使用的完全一致，稳定到可以直接写脚本调用。

```
GET    /api/health                    版本、路径、运行时长、进行中任务
GET    /api/meta/options              格式、平台、颜色、各项上限
GET    /api/meta/i18n                 报告正文的 EN/ZH 对照表
GET    /api/meta/stats                请求数、缓存命中、重试、字节数
GET    /api/quota · /api/cache        配额快照；缓存位置

POST   /api/jobs/analyze              → 202 {job_id}
POST   /api/jobs/compare              → 202 {job_id}
GET    /api/jobs/:id                  状态与进度日志
GET    /api/jobs/:id/result           报告 JSON
GET    /api/jobs/:id/events           SSE：progress / stage-done / done
GET    /api/jobs/:id/export?format=   json|md|txt|html|csv|xlsx|pdf
POST   /api/jobs/:id/cancel

GET    /api/subscriptions             POST 新增 · PATCH/DELETE /:ref
POST   /api/subscriptions/analyze     批量，可按条目覆盖参数
POST   /api/subscriptions/:ref/analyze

GET    /api/config                    令牌脱敏为 {set, masked}
PATCH  /api/config                    {"sets": {...}}，校验在服务端完成
GET/PUT /api/config/raw               POST /api/config/reset

GET    /api/llms                      POST 新建 · PATCH/DELETE /:name
POST   /api/llms/:name/activate · /test

GET    /api/usage?days= · /records    DELETE /api/usage
                                  /records 支持 days=，让日志与图表同窗口
GET    /api/completion/:shell         bash | zsh | fish

GET    /*                             构建后的控制台（前端路由回退）
```

### 任务请求体

`POST /api/jobs/analyze` 与 `POST /api/jobs/compare` 接受同一组单次运行选项，
每一项只对该任务生效，会覆盖配置文件里的值：

| 字段 | 含义 |
|------|------|
| `since_days`、`from`、`to` | 时间窗 |
| `platform`、`branch`、`lang` | 读哪个平台、哪个分支，用什么语言输出 |
| `max_pages`、`api_base`、`proxy`、`token` | 传输层 |
| `llm`、`llm_mode`、`no_cache` | 分析本身 |
| `filters` | `["author:alice", "label:bug"]` |
| `export` | `["show_commits=false", "pdf.margin=40"]`——本次运行的导出设置 |

结果里的 `llm_provider` 是真正写出摘要的提供商；用本地摘要时它为空，
这在 LLM 失败时是唯一诚实的说法。对比结果的 trend summary 也带同一个字段。

`export` 也接受对象写法 `{"show_commits": false, "pdf": {"margin": 40}}`。
导出器接受的键名由 `GET /api/meta/options` 的 `export_keys` 给出；
不认识的键会被忽略，因此发送前最好先查这份清单。

密钥永远不会离开进程：配置里的令牌与提供商的 API key 只以
`{"set": true, "masked": "…3f2a"}` 形式返回；写入空字符串表示清除。
