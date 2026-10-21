# GitSight Web 控制台

[English](README.md) | 中文

`gs serve` 提供的浏览器界面。它只是 CLI 同一套分析、导出与配置引擎之上的
前端——没有任何逻辑的第二份实现，读写的是同一份
`~/.config/gitsight/config.json`。

```bash
gs serve                  # → http://127.0.0.1:8787
```

## 页面

| 页面 | 覆盖内容 |
|------|---------|
| **概览** | 服务状态、API 配额、响应缓存、最近任务 |
| **分析** | `gs analyze` 的全部选项、实时进度、可交互报告、7 种格式下载 |
| **对比** | 两个周期并排对比，含各项变化率、趋势摘要与逐条的变化明细 |
| **批量** | 单次最多 16 个仓库，8 个并行，其中任意两个可并排对比 |
| **订阅** | `gs watch` 的增删改查，支持单个与批量分析 |
| **用量** | 每日与按提供商的 Token 消耗，以及原始调用记录 |
| **LLM 提供商** | 提供商增删改查、协议选择、连通性测试 |
| **设置** | 全部 `gs config --set` 配置项，含 33 项导出选项 |
| **补全脚本** | 内嵌的 bash/zsh/fish 脚本 |

## 单次运行选项

所有会发起任务的页面都带有 CLI 接受的同一批选项，因此在浏览器里发起的一次
分析与在命令行里发起的是同一种配置。留空表示"沿用配置文件里的值"，
与不传该选项时 CLI 的行为完全一致。

| 选项 | 位置 |
|------|------|
| `--since` / `--from` / `--to` | 分析、批量 |
| `--platform` `--branch` `--lang` | 分析、对比、批量 |
| `--max-pages` `--api-base` `--proxy` `--token` | 分析、对比、批量 |
| `--llm` `--llm-mode` | 分析、对比、批量、订阅 |
| `--filter` | 分析、对比、批量 |
| `--no-cache` | 分析、对比、批量、订阅 |
| `--export key=value` | 分析、对比、批量 |
| `--parallel` | 批量、订阅 |

其中 `--export` 值得单独说明：它只覆盖本次运行的导出设置，不会改动配置文件。
控制台填写的 `key=value` 与 CLI 完全一致，每行一条；守护进程通过
`GET /api/meta/options` 的 `export_keys` 给出导出器真正支持的键名，
因此写错的键会被明确提示，而不会被悄悄忽略。在表单里点击一个键名即可自动
补上 `key=`。

## 控制台独有的能力

有三件事是一次性 CLI 调用做不到的：

- **实时进度。** 分析过程通过 SSE 推送到浏览器，运行中的任务可随时取消。
- **重新渲染，而非重新抓取。** 分析完成后结果保留在内存里，因此切换导出
  格式只是重新渲染已存结果，不会再次访问平台。
- **逐条对比。** 两个周期之间按条目比对——最近这段时间新开、合并、关闭的
  PR；批量结果里的任意两个仓库也能直接并排，无需再跑一次。

## 快速开始

### 构建

```bash
# 构建控制台
npm --prefix web install
npm --prefix web run build
cp -r web/dist build/web

# 或一步完成（需要 Node）：
cmake -B build -DGITSIGHT_BUILD_WEB=ON && cmake --build build
```

使用 `GITSIGHT_BUILD_WEB=ON` 时，守护进程从内嵌的 `build/web` 目录提供资源，
单个二进制无需额外的 Web 服务器。

### 开发模式

```bash
# API 在 :8787，Vite 在 :5173 并代理 /api
gs serve --port 8787 &
npm --prefix web run dev
```

Vite 把 `/api` 代理到守护进程，因此浏览器始终只与一个源通信，相对 API 路径、
Cookie 与 SSE 流的行为与生产环境完全一致。

### 安全

`gs serve` 默认只监听回环地址且**没有鉴权**——它会读写保存访问令牌的配置
文件。只有在你信任的网络里才使用 `--host 0.0.0.0`。

## 技术栈

- **React 19** + **TypeScript 5.7**，由 **Vite 6** 打包
- **TanStack Router** 负责路由，**TanStack Query** 管理服务端状态
- **Tailwind CSS 4** 样式，**Base UI** 提供无障碍基础组件
- **Recharts** 图表，**i18next** 中英文界面
- **Vitest** + **Testing Library** 测试

构建需要 Node 20 或更高版本。

## 目录结构

```
web/
├── src/
│   ├── routes/        # 每个页面一个文件
│   ├── features/      # 页面级组件（analyze、settings 等）
│   ├── components/    # ui/ 基础组件、layout/、report/
│   ├── lib/           # api 客户端、格式化、解析器、类型
│   ├── i18n/          # 翻译初始化与 en/zh 词条
│   ├── hooks/         # 共享 hook（如 useJob）
│   ├── stores/        # zustand UI 状态
│   └── test/          # 集成测试与 fixtures
└── vitest.config.ts
```

## 测试

```bash
npm --prefix web run test        # 单次运行
npm --prefix web run test:watch  # 监听模式
npm --prefix web run typecheck
npm --prefix web run lint
```

集成测试位于 `src/test`，会在被 mock 的 HTTP 服务
（`src/test/fakeServer.ts`）之上挂载真实页面；单元测试与被测代码放在一起。

> 测试套件是 I/O 密集型的。仅 `recharts` 的模块加载就占据了冷启动的大部分
> 时间；在慢速挂载点上（例如 WSL 的 `/mnt/...` 盘符），Vitest worker 可能在
> 转换过程中超时。把仓库放在原生文件系统上可以避免这个问题。

## 相关文档

- HTTP 接口参考：[docs/usage.zh-CN.md](../docs/usage.zh-CN.md#http-接口)
- 配置项：[docs/configuration.zh-CN.md](../docs/configuration.zh-CN.md)
- 架构：[docs/architecture.zh-CN.md](../docs/architecture.zh-CN.md)
- 项目总览：[README.zh-CN.md](../README.zh-CN.md)
