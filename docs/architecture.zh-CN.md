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
