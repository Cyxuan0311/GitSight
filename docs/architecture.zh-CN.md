# 架构说明

## 概述

GitSight 是一个用 C17 编写的 CLI 工具，用于获取 GitHub 仓库活动数据并生成结构化报告，可选 LLM 增强分析。

## 模块结构

```
gs
 +-- cli (getopt_long)              -- 命令行参数解析
 +-- config (cJSON)                 -- 配置文件读写/合并
 +-- color (ANSI + isatty)          -- 彩色输出和终端检测
 +-- anim (pthread)                 -- 解耦的加载动画模块：
 |                                   line、盲文点/填充、bounce、
 |                                   unicode、三角、象限、进度条
 +-- chart                          -- 解耦的 unicode 柱状图渲染器
 |                                   （横向条形 + 纵向柱状）
 +-- usage                          -- 本地 LLM 用量日志：记录/加载、
 |                                   按提供商或按天聚合
 +-- platform/                      -- 平台抽象层
 |    +-- client                    -- 抽象接口（函数指针表）
 |    +-- profile                   -- 平台配置表（认证头 + 默认 API 地址）
 |    +-- github                    -- 共享的"兼容 GitHub"驱动，供
 |    |                               github / codeberg / gitea / forgejo 使用
 |    +-- gitlab                    -- GitLab REST API (libcurl + cJSON)
 +-- llm/                           -- LLM 集成
 |    +-- client                    -- LLM 客户端抽象接口
 |    +-- openai                    -- OpenAI 兼容 API (libcurl + cJSON)
 +-- analyzer/engine                -- 统计 + LLM Prompt 构建
 +-- export/                        -- 报告导出器
 |    +-- json                      -- cJSON
 |    +-- md                        -- Markdown
 |    +-- txt                       -- ASCII 表格
 +-- container (动态数组)            -- 泛型动态数组
 +-- models                         -- 数据结构（PR, Commit, Issue, Report）
 +-- error                          -- 错误码和描述
```

## 数据流

```
用户输入 → cli_parse → Config → PlatformClient
                                      |
                            +---------+---------+
                            |         |         |
                      fetch_prs  fetch_commits  fetch_issues
                            |         |         |
                            +----+----+---------+
                                 |
                           analyzer_run()
                            /           \
                      统计摘要        [LLM 可选]
                            |           |
                            +-----+-----+
                                  |
                              Report 结构
                                  |
                              Exporter (json/md/txt)
                                  |
                            stdout / 文件
```

## 扩展新平台

平台抽象基于函数指针表（`PlatformClient`）。

### 兼容 GitHub 的平台（Codeberg、Gitea、Forgejo 等）

这些平台暴露与 GitHub 相同的 REST 接口
（`/repos/{owner}/{repo}/pulls|issues|commits|releases|contributors`）。
新增一个平台只需在 `src/platform/profile.c` 中加一行：

```c
{ "mynamespace", "https://host.example.com/api/v1", "token" },
```

并同步更新 `config.c`（`set_top_level` 平台校验）、`src/main.c` 的主机自动
识别、CLI 帮助文本和 shell 补全。

### 结构不同的平台（如 GitLab）

1. 创建 `include/gitsight/platform/<name>.h` 和 `src/platform/<name>.c`
2. 实现 6 个函数：`fetch_prs`, `fetch_commits`, `fetch_issues`,
   `fetch_releases`, `fetch_meta`, `fetch_contributors`, `destroy`
3. 在 `src/platform/client.c` 中注册：
   ```c
   ErrorCode platform_client_init(PlatformClient *client, const Config *cfg, ...) {
       if (strcmp(cfg->platform, "gitlab") == 0)
           return gitlab_client_init(client, cfg, owner, repo);
       return github_client_init(client, cfg, owner, repo);
   }
   ```
4. 同步更新 `config.c`（令牌路由 + 平台校验）、`src/main.c` 的主机自动识别、
   CLI 帮助文本和 shell 补全。

检测逻辑基于 URL 主机名模式（如 `gitlab.com` → GitLab、`codeberg.org` →
Codeberg）或显式 `--platform` CLI 参数。分页是通用的：`http_get_paginated()`
同时处理 `Link` 头（GitHub/Gitea/Forgejo）与 `X-Next-Page` 头（GitLab）。

### 添加新的 LLM 提供商

1. 实现 `include/gitsight/llm/client.h` 中的 `llm_generate()` 签名
2. 在 LLM 配置中添加提供商选择（如 `"provider": "openai"` / `"provider": "anthropic"`）
3. 将调用路由到对应的实现

## 关键设计决策

- **函数指针表**（非继承）实现平台抽象 -- 保持 C 代码简洁透明
- **cJSON 源码内嵌** -- 避免外部依赖和构建时的网络问题
- **libcurl 做 HTTP** -- 久经考验，支持所有代理类型，广泛可用
- **单二进制输出** -- 尽量静态链接，`gs` 一个文件就够了
- **进度走 stderr，数据走 stdout** -- 无需 `--quiet` 即可干净管道

## Web 控制台

`gs serve` 启动的 HTTP 守护进程直接复用 CLI 已有的代码路径。分析、导出、
配置逻辑没有第二份实现 —— 服务端只是一层传输。

### 为什么接缝这么薄

四处重构让既有代码在行为不变的前提下变得可复用：

| 重构 | 原因 |
|------|------|
| `src/cmd.h` 中的 `AnalysisResult` | `analyze_repo()` 原先在返回前释放了各条目 `Vector`，调用方只能拿到汇总的 `Report`。现在交还 `Report` 加六个 `Vector`，正是 `Exporter.write_report` 需要的形态。 |
| `analysis_render()` | 用 `open_memstream()` 把导出器的 `FILE*` 接到堆内存缓冲区上。于是 7 种格式（含二进制的 `xlsx` 与 `pdf`）走同一条代码路径，完全不碰文件系统。 |
| `anim_set_listener()` | 每个阶段切换本来就经过 `anim_start`/`anim_update`/`anim_stop`。装一个监听器就把动画变成了结构化进度流，无需额外埋点。CLI 不设置它。 |
| 带线程局部绑定的 `cancel.h` | `CURLOPT_XFERINFOFUNCTION` 可中断在途传输。标志按工作线程绑定，取消一个任务不会波及并行的其他仓库。 |

`Options` 本就持有 CLI 接受的全部 analyze 选项，因此请求体可直接反序列化进
它，`analyze_repo()` 的调用方式完全不变。

### 并发模型

`gs serve` 面向单用户、默认只监听回环，因此模型刻意保持保守：

- **同一时刻只跑一次分析。** 一把全局互斥锁串行化所有触及分析路径全局状态的
  操作：语言全局、用量日志、HTTP 响应缓存与配置文件。这些工作都是网络密集
  型的，单一写入者即可让它们保持一致，无需给既有模块塞进新的锁。
- **跨仓库并行。** 单个任务内，仓库各自跑在最多 `batch_max_threads` 个线程
  上；每个仓库内部**串行**抓取 7 类资源，于是 N 个仓库只花 N 个线程而非 N*7。
- **按任务取消。** 取消标志绑定到调用线程，中断一个仓库不会影响其邻居。
- **每连接一线程。** SSE 流可能阻塞数分钟，因此每个连接独立成线程，而不是
  共用选择器循环。

### 任务与导出

完成的任务持有自己的 `AnalysisResult`。这正是
`GET /api/jobs/:id/export?format=pdf` 得以存在的原因：7 种格式中的任意一种
都能随时从内存渲染，不必重新抓取，也不受 CLI 对 stdout 施加的
`xlsx`/`pdf` 需要 `-o` 的限制。导出参数在任务运行时快照留存，因此之后修改
设置不会改变已下载的产物。

SSE 处理器而非 `job_finish()` 负责发出终态事件，这样即使浏览器在工作完成
之后才连上，也仍然恰好收到一个 `done`。

### 新增接口

1. 在对应的 `api_*.c` 中编写处理函数，签名为 `(Request*, Response*)`，
   使用 `res_*` 系列辅助函数。配置请通过 `server_load_config()` 读取，以便
   在状态锁内拿到副本。
2. 在 `api_register_*()` 中用 `router_add()` 注册。
3. 在 `tests/test_server.sh` 中加一条 curl 断言 —— 该脚本会借助
   `GS_FIXTURE_DIR` 离线跑通整个 API。
