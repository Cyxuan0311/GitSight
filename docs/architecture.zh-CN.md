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
 +-- platform/                      -- 平台抽象层
 |    +-- client                    -- 抽象接口（函数指针表）
 |    +-- github                    -- GitHub REST API (libcurl + cJSON)
 |    +-- gitlab                    -- （计划中）
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

### 添加 GitLab 支持

1. 创建 `include/gitsight/platform/gitlab.h` 和 `src/platform/gitlab.c`
2. 实现 4 个函数：`fetch_prs`, `fetch_commits`, `fetch_issues`, `destroy`
3. 在 `src/platform/client.c` 中注册：
   ```c
   ErrorCode platform_client_init(PlatformClient *client, const Config *cfg, ...) {
       if (/* 是 GitHub */)
           return github_client_init(client, cfg, owner, repo);
       else if (/* 是 GitLab */)
           return gitlab_client_init(client, cfg, owner, repo);
       ...
   }
   ```

检测逻辑可基于 URL 模式（如 URL 中包含 `gitlab.com`）或 `--platform` CLI 参数。

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
