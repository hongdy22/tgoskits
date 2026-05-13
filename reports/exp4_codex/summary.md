# Exp4 Codex on StarryOS 总结报告

日期：2026-05-13

## 1. 实验目标

本实验的目标是让 OpenAI Codex CLI 可以在 StarryOS x86_64 QEMU guest 中启动并完成基础 coding-agent 工作流。最初目标不是完整复刻 Linux 桌面环境里的 Codex，而是先建立一个可验证闭环：

1. StarryOS 能运行 Codex CLI 的 Linux x86_64 musl 二进制。
2. `codex --version`、`codex --help`、`codex exec --help`、`codex login status` 等本地命令可以稳定执行。
3. 在注入 `auth.json`、CA 证书和代理配置后，`codex exec` 可以联网请求模型。
4. Codex 可以在 guest 内读写文件、调用 `git`/`rg`，并通过 guest 内 `git diff` 验证修改。
5. 最终在 StarryOS guest 内直接 clone TGOSKits 仓库，让 Codex 尝试定位一个小的 syscall/内核语义问题，展示“在 StarryOS 中使用 Codex 改进 StarryOS”的 self-evolve 雏形。

## 2. 报告资料目录

本目录整理了实验过程中最重要的文档型材料，不包含 `auth.json`、rootfs 镜像、QEMU build tree、运行日志或大二进制。

```text
target/reports/exp4_codex/
  summary.md                         # 本总结报告
  plan.md                            # 原始分阶段计划
  demo/
    demo.md        # 当前可用的 self-evolve demo 简明步骤
  notes/
    current_status_and_demo.md       # 阶段性状态和演示说明
    stage_a_report.md                # 基线环境和可观测性
    stage_b_report.md                # Codex 二进制注入和离线 smoke
    stage_c_report.md                # auth/TLS/HTTPS/最小在线 Codex
    stage_d_report.md                # 小 workspace 读写、rg 和 shell 闭环
    stage_e_report.md                # 扩大到 TGOSKits 源码子集
    stage_f_report.md                # Codex 日志驱动的兼容性修复
    stage_g_report.md                # Codex smoke 接入测试套件的历史方案
    stage_h_report.md                # Codex 在 TGOSKits 子任务上试跑
    stage_i_report.md                # 演示整理与 failure-path 清理
```

其中 `stage_g_report.md` 记录的是一个历史尝试：把 Codex smoke 放进 StarryOS test-suit。后续根据 review 意见，下载 Codex 会消耗自部署主机流量，因此最终 PR 形态改成 `examples/starry/codex-cli` 下的 opt-in example，不再进入测试套件或 CI。

## 3. 实验环境

主要环境如下：

- 仓库：`/home/threetu33/os_biglabB_task2/tgoskits`
- 初始开发分支：`exp4_codex`
- 后续 PR 分支：`test/starryos-prebuilt-cli-smoke`
- Guest：StarryOS x86_64 QEMU
- Codex CLI：`@openai/codex@0.115.0-linux-x64` 包内的 Linux x86_64 musl 二进制
- 辅助工具：`rg`、`git`、CA 证书、QEMU user network
- 本地认证：host 侧 `target/auth.json` 注入 guest `/root/.codex/auth.json`
- 本地代理：演示环境使用 `http://172.31.48.1:7890`

当前推荐的 Codex rootfs 准备入口是：

```bash
examples/starry/codex-cli/prepare_codex_rootfs.sh \
  --output-rootfs tmp/axbuild/rootfs/rootfs-x86_64-codex-online.img \
  --auth-json target/auth.json \
  --proxy http://172.31.48.1:7890
```

这会在本地生成 ignored rootfs，不提交到仓库。

## 4. 实验推进过程

实验大致分成九个阶段：

| 阶段 | 目标 | 关键结果 |
| --- | --- | --- |
| A | 确认可观测性和基础环境 | 验证 StarryOS 已具备 ELF、rootfs、网络、procfs、poll/epoll 等基础能力 |
| B | 注入 Codex/rg 并跑离线 smoke | `codex --version/help`、`codex exec --help` 和 `rg` 可运行 |
| C | 注入 auth、CA 和代理，跑最小在线请求 | `codex exec` 能请求模型，暴露 `epoll_wait` 用户缓冲区 page fault |
| D | 在小 workspace 中读写文件 | 暴露 `poll/ppoll` 用户缓冲区 race，并验证 Codex 能读写文件 |
| E | 扩大到 TGOSKits 源码子集 | Codex 能读取真实 StarryOS/TGOSKits 路径并生成受控说明文件 |
| F | 按 Codex 日志补齐兼容性缺口 | 修复 `getcwd`、`PR_CAPBSET_READ`、TCP keepalive、zombie 查询等 Linux ABI 差异 |
| G | 固化离线 smoke 的测试形态 | 曾尝试接入 test-suit，后按 review 调整为 example |
| H | 让 Codex 完成 TGOSKits 子任务 | Codex 能读仓库上下文、创建文件，并由 guest 内 `git diff` 验证 |
| I | 整理 demo 和 failure-path | 修复 tmpfs cwd cleanup panic，并整理最终演示流程 |

这个推进顺序比较重要：不是一上来追求完整 TUI 或 sandbox，而是用 Codex 真实运行路径不断暴露 StarryOS 的 Linux 兼容性缺口，再把缺口拆成可 review 的小 PR。

## 5. 发现并修复的代表性问题

### 5.1 `epoll_wait` 等待期间持有用户缓冲区引用

问题现象：在线 `codex exec` 请求模型时，StarryOS 在 `Epoll::poll_events` 附近触发 kernel page fault。

根因：旧实现把用户态 `events` buffer 转成内核可变引用后跨阻塞点保存。Codex 是多线程程序，等待期间其他线程可能取消映射或改变该用户缓冲区。唤醒后内核直接写旧地址，导致 page fault。

修复思路：等待期间只写 kernel-side 临时 buffer，返回前再做 checked user-copy。用户缓冲区失效时返回 `EFAULT`，而不是 panic。

回归测试：`bug-epoll-wait-user-buffer-race`。

### 5.2 `poll/ppoll` 等待期间持有用户 `pollfd` 引用

问题现象：小 workspace 读写阶段暴露 `poll(2)` 阻塞路径 page fault。

根因：旧实现保存用户态 `pollfd` 的 `revents` 字段引用并跨阻塞点使用。用户线程在等待期间 `munmap` 后，唤醒路径写回旧用户地址。

修复思路：入口 copy-in 到内核 `Vec<pollfd>`，等待期间只改内核副本；返回时按 checked user-copy 写回 `revents`。

回归测试：`bug-poll-wait-user-buffer-race`。

### 5.3 raw `getcwd` ABI 与 Linux 不一致

问题现象：普通 `git status` / `git diff` 仓库发现路径不稳定，早期需要用 `--git-dir` / `--work-tree` 绕过。

根因：StarryOS raw `getcwd` 成功返回值和 Linux ABI 不一致。Linux 返回包含末尾 NUL 的长度，并且错误优先级需要先判断路径长度是否超过 buffer size。

修复思路：按 Linux raw syscall 语义返回 `cwd.len() + 1`；小 buffer 返回 `ERANGE`；用户写入失败返回 `EFAULT`。

回归测试：`bug-getcwd-syscall-return`。

### 5.4 `prctl(PR_CAPBSET_READ)` 兼容性缺口

问题现象：Codex/curl 路径会触发 `sys_prctl: unsupported option 23`。

根因：StarryOS 缺少最小 capability bounding set 查询支持。后续 review 还指出参数不能先截断为 `u32`，否则高 32 位非零的非法 capability 会被误接受。

修复思路：对完整 syscall 参数做 capability 有效性检查。StarryOS 当前没有真实 capability dropping，因此有效 capability 返回 `1`，超出 `CAP_LAST_CAP` 返回 `EINVAL`。

回归测试：`bug-prctl-capbset-read`。

### 5.5 TCP keepalive/user-timeout sockopts

问题现象：curl/hyper 会设置 `TCP_KEEPIDLE`、`TCP_KEEPINTVL`、`TCP_KEEPCNT`、`TCP_USER_TIMEOUT`，旧实现返回 `ENOPROTOOPT` 或无法用 `getsockopt` 读回。

根因：这些 TCP 选项是很多 Linux 网络客户端的可观察 ABI。只让 `setsockopt` 成功但不保存状态、不能 `getsockopt` 读回，仍然与 Linux 不一致。

修复思路：在 axnet-ng socket 状态中保存这些选项，补齐 set/get 和 Linux 范围检查，并把 `TCP_KEEPIDLE` 接入实际 keepalive idle timeout。

回归测试：`bug-tcp-keepalive-options`。

### 5.6 未回收 zombie child 的可见性

问题现象：Codex 写文件任务结束后，清理子进程时可能报 `No such process (os error 3)`。

根因：Linux 中未被 `waitpid` 回收的 zombie child 对 `getpgid`、`getsid`、`kill(pid, 0)`、`kill(pid, SIGKILL)` 仍可见。StarryOS 的进程查询路径只看已释放的 `ProcessData`，导致 zombie 在 wait 前过早不可见。

修复思路：保留 unreaped zombie 的 `Process` 和凭据快照，让查询和信号权限检查在 wait 前仍能找到它。

回归测试：`bug-zombie-process-queries`，包括非 root 同 UID kill 路径。

### 5.7 tmpfs cwd cleanup failure-path panic

问题现象：子进程 cwd 指向 tmpfs 目录时，父进程先 `rmdir`，子进程退出清理 cwd 可能在 atomic context 中获取睡眠锁并 panic。

根因：目录项清理发生在 `MemoryNode::drop` 这类 failure/drop 路径中，锁类型和执行上下文不匹配。

修复思路：`MemoryNode::drop` 不再清理目录 entries；tmpfs inode slab/metadata 改用非睡眠锁；`unlink`/`rename` 在正常 syscall 上下文提前清理被移除目录项。

回归测试：`bug-tmpfs-cwd-drop-safe`。

## 6. PR 拆分与最终提交形态

本实验最终拆成中等粒度 PR，既方便 reviewer 对照 Linux 语义 review，也方便本地验证：

| PR | 主题 | 状态与说明 |
| --- | --- | --- |
| #523 | poll/epoll wait 用户缓冲区安全 | 已接收。解决阻塞期间用户 buffer 被 unmap 后 kernel page fault |
| #524 | Codex/git/curl 路径的 CLI 兼容缺口 | 已接收。覆盖 `getcwd`、`PR_CAPBSET_READ`、TCP keepalive、zombie 可见性 |
| #525 | tmpfs cwd cleanup panic | 已接收。处理 failure/drop 路径 panic |
| #575 | Codex CLI demo flow | 已接收。根据 review 调整为 `examples/starry/codex-cli` opt-in example，不进入 test-suit/CI |

## 7. 最终 demo 流程

当前本地已验证的 self-evolve demo 文档是：

```text
target/reports/exp4_codex/demo/self_evolve_demo_steps.md
```

核心一键命令：

```bash
PATH="$PWD/target/codex/qemu-build-x86_64:$PWD/target/codex/qemu-build-x86_64-user:$PATH" \
cargo xtask starry qemu \
  --arch x86_64 \
  --qemu-config target/codex/qemu/qemu-x86_64-codex-tgoskits-syscall-hunt.toml \
  --rootfs tmp/axbuild/rootfs/rootfs-x86_64-codex-online.img
```

这个 demo 会在 StarryOS guest 中：

1. 设置 `HOME`、`CODEX_HOME`、CA、代理和 PATH。
2. 检查 `git`、`rg`、`codex`。
3. 直接 clone `https://github.com/rcore-os/tgoskits.git` 的 `dev` 分支。
4. 把 syscall bug-hunt prompt 交给 `codex exec`。
5. 要求 Codex 只选择一个小的 syscall/内核语义目标，不做大范围重构。
6. 输出候选 bug、检查过的文件、修改文件和验证命令。
7. 打印 `git status` 和 `git diff --stat`。

成功标记：

```text
STARRY_TGOSKITS_SYSCALL_HUNT_PASSED
```

实际演示中，Codex 曾在 guest 内选择 `getdents64` 目录读取参数语义作为候选目标，并生成了 StarryOS kernel/test-suit 相关改动。这说明 demo 不只是“让模型回答 hello”，而是能在 StarryOS guest 内读取真实仓库、执行检索命令、形成小范围修复思路。

## 8. 验证策略

本实验采用三层验证：

1. **内核 bugfix 回归测试**  
   对 `poll`、`epoll_wait`、`getcwd`、`prctl`、TCP sockopts、zombie 查询、tmpfs cwd cleanup 等问题补最小 C test case，并运行 StarryOS x86_64 QEMU bugfix group。

2. **工程质量检查**  
   代码变更后运行 `cargo fmt` 和相关 crate 的 `cargo xtask clippy --package ...`。对 axbuild 相关变更运行 `cargo test -p axbuild test::case`。

3. **Codex demo 验收**  
   用 guest 内 `grep`、`git status --short`、`git diff` 或固定 marker 验证 Codex 的输出和文件改动，而不是只看 host 侧日志。

## 9. 当前边界

当前可以明确说已经完成：

- Codex CLI Linux x86_64 musl 二进制可以在 StarryOS x86_64 QEMU 中启动。
- 离线 help/login/status/git/rg 路径可运行。
- 带本地 auth 和代理时，`codex exec` 可以在线请求模型。
- Codex 可以在 guest 内读取真实 TGOSKits 仓库、提出小范围 syscall/内核语义问题，并产生受控文件修改。
- 围绕 Codex 路径暴露的多个 Linux ABI 和 failure-path 问题已拆 PR 修复。

未来方向：

- 完整支持 Codex TUI。
- 支持 Codex 默认 Linux sandbox、landlock/seccomp 语义。
- 考虑高并发。

## 10. 思考与经验

这次实验最有价值的点不是单纯“跑起一个大程序”，而是把 Codex CLI 当成真实 Linux 应用压力源，逼出了很多平时小测试不容易覆盖的边界：

- 多线程应用会让用户指针 TOCTOU 问题变得真实可见。
- `git`/`curl` 这类工具依赖很多看似很小的 Linux ABI 细节，例如 raw `getcwd` 返回值、TCP keepalive 选项、zombie 进程查询。
- failure path 和 drop path 需要和正常 syscall path 分开思考，尤其是 tmpfs/pseudofs 的锁语义。

最终结果是：StarryOS 已经具备运行 Codex CLI 并完成小型 coding-agent 闭环的能力，同时相关内核兼容性修复也被拆成了更容易 review 和维护的独立改动。
