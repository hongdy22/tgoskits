# Big Lab B 总结报告

> 洪德垚 计36 2022010039
> 日期：2026-05-14

## 一、整体概述

Big Lab B 可以分成两个层次：

- `task1_tutorial`：热身任务，目标是熟悉 ArceOS/TGOSKits 的构建、运行、测试方式，以及 `no_std`、VFS、内存管理、syscall 等基础层次。
- `exp1` 到 `exp4`：正式 Task2，目标是借助 Codex pipeline 持续发现、验证、修复 StarryOS 的 Linux 兼容性问题，并最终让 Codex CLI 本身可以在 StarryOS x86_64 QEMU guest 中运行，形成一个“在 StarryOS 中使用 Codex 改进 StarryOS”的闭环雏形。

这组实验不是单纯完成几个独立功能，而是逐步推进一条主线：

```text
熟悉系统结构
  -> 搭建 Codex 自动迭代 pipeline
  -> 批量修复 syscall 兼容性问题
  -> 面向 BusyBox 真实应用补齐 Linux ABI
  -> 运行 Codex CLI 并用它反向测试 StarryOS
```

从工程结果看，实验产出主要包括：

- 完成 ArceOS tutorial 中的多个系统层实验。
- 搭建了一套 Syncer / Developer / Reviewer / Committer 闭环 pipeline。
- 基于 pipeline 在 Exp2 中拆出并合入 8 个 syscall 兼容性 PR。
- 基于 pipeline 在 Exp3 中拆出并合入 7 个 BusyBox 兼容性 PR。
- 在 Exp4 中围绕 Codex CLI 修复了多处 StarryOS 内核兼容性和 failure-path 问题，并整理出 opt-in demo/example 流程。

本总结重点在于整理实验方法、代表性问题、根因分析和整体思考。各子报告与原始材料可以按下面的索引阅读：

- Task1 tutorial：<https://github.com/hongdy22/tgoskits/tree/dev/reports/task1_tutorial>
- Exp1 Codex pipeline：<https://github.com/hongdy22/tgoskits/tree/dev/reports/exp1_pipeline>
- Exp2 syscall 兼容性：<https://github.com/hongdy22/tgoskits/tree/dev/reports/exp2_syscall>
- Exp3 BusyBox 兼容性：<https://github.com/hongdy22/tgoskits/tree/dev/reports/exp3_busybox>
- Exp4 Codex on StarryOS：<https://github.com/hongdy22/tgoskits/tree/dev/reports/exp4_codex>

## 二、Task1：从教学实验建立系统分层意识

Task1 是热身，但它对后续实验很关键。通过 ArceOS tutorial，可以先建立几个基本判断：

1. 一个看似简单的用户态行为，往往跨越多个系统层。
2. `no_std + alloc` 环境中，很多桌面 Rust 默认能力需要显式补齐。
3. 文件系统、内存映射和分配器问题不能只看 API 名称，要沿调用链看实现位置。

例如 `exercise-ramfs-rename` 表面上是实现 `std::fs::rename`，但实际调用链是：

```text
axstd -> axfs -> axfs_ramfs
```

只在 `axfs_ramfs` 的目录节点中实现 rename 还不够，上层组合根目录也必须正确转发该操作。这个实验让我认识到，后续修复 StarryOS 时不能只搜到一个函数就开始改，而要先确认 syscall、VFS 分发层、具体文件系统节点和测试入口之间的关系。

再比如 `exercise-sysmap` 中实现 `mmap`，核心并不只是“分配一段内存”，还包括：

- `length` 和 `offset` 的合法性；
- `PROT_READ/WRITE/EXEC` 到内核映射权限的转换；
- `MAP_PRIVATE/MAP_SHARED/MAP_FIXED/MAP_ANONYMOUS` 的分支；
- 文件内容是否正确写入用户地址空间；
- 返回地址是否能被用户态程序继续访问。

这些经验后来直接迁移到 Exp2/Exp4 中。比如 Exp2 修复 `mmap(fd=0)`，Exp4 修复用户缓冲区跨阻塞点访问，背后都依赖对“用户态参数进入内核后必须经过严格边界处理”的理解。

## 三、Exp1：Codex 闭环 Pipeline 的设计

Exp1 的目标不是让 Codex 偶尔写出一个补丁，而是把它放进一个可重复的工程闭环中。最终 pipeline 被拆成四个角色：

```text
Syncer    : 同步 tgoskits 到 upstream/dev
Developer : 选择一个小目标，建立 Linux/StarryOS 差分，写测试和修复
Reviewer  : 独立复核 Linux 语义、测试覆盖、补丁风险
Committer : 在 Reviewer PASS 后确定性提交、push、记录状态
```

这个设计的核心取舍是：让模型做需要理解和判断的部分，让脚本做确定性的机械部分。

模型适合做：

- 阅读代码并定位可能根因；
- 设计最小 C regression；
- 对比 Linux 与 StarryOS 行为；
- 解释修复方案；
- 根据 review 意见继续迭代。

脚本更适合做：

- 同步分支；
- 拼接 prompt；
- 校验 JSON schema；
- 保存 patch、日志、journal；
- 执行 `git diff --check`；
- 创建分支、提交、push；
- 记录已经 PASS 的 target，避免重复劳动。

Pipeline 中最重要的工程护栏有三类。

第一类是 **结构化输出**。Syncer、Developer、Reviewer、Committer 都有 JSON schema。这样 orchestrator 不需要猜自然语言结论，可以稳定读取 `decision`、`target`、`evidence`、`validation` 等字段。

第二类是 **状态记忆**。`journal.md` 和 `passed_commits.json` 会进入下一轮 prompt，防止模型重复修复已经 PASS 的问题，也让每轮都能站在前一轮结果上继续。

第三类是 **Reviewer 否决权**。Reviewer 不是形式检查，而是会基于 Linux/POSIX/RFC/项目实现复核。例如后续多次出现“测试过了但语义仍不完整”的情况：TCP keepalive set 后没有真正影响 socket 状态、`PR_CAPBSET_READ` 参数被提前截断、`epoll_wait` 参数校验时机不符合 Linux 等。这些问题如果没有独立 review，很容易以“能跑通当前 case”的形式混进仓库。

Exp1 的价值在后续实验中体现得很明显：Exp2 和 Exp3 的 15 个已合入 PR，并不是一次性手工找出来的，而是由这套闭环不断选题、验证、修复、审查和拆分形成的。

## 四、Exp2：Syscall 兼容性修复

Exp2 面向 StarryOS syscall 语义差分。它的思路是：不先追求跑通某个大应用，而是让 Codex 从较小的 syscall 语义点出发，建立 Linux 基准和 StarryOS 差分，再补最小修复和长期回归测试。

最终形成 8 个已合入 PR：

| PR | 主题 |
| --- | --- |
| [#449](https://github.com/rcore-os/tgoskits/pull/449) | `linkat` flags 与 symlink no-follow/follow 语义 |
| [#450](https://github.com/rcore-os/tgoskits/pull/450) | `mmap` fd 0 与 anonymous fd 语义 |
| [#451](https://github.com/rcore-os/tgoskits/pull/451) | `renameat2(RENAME_NOREPLACE)` |
| [#460](https://github.com/rcore-os/tgoskits/pull/460) | `faccessat2` mode/flags 校验 |
| [#461](https://github.com/rcore-os/tgoskits/pull/461) | `wait4` invalid options |
| [#462](https://github.com/rcore-os/tgoskits/pull/462) | `fchmodat2` flags 与 legacy dispatch |
| [#463](https://github.com/rcore-os/tgoskits/pull/463) | `readlinkat(size=0)` |
| [#464](https://github.com/rcore-os/tgoskits/pull/464) | `mknodat` mode type bits |

### 4.1 典型案例：`mmap(fd=0)` 不是无效 fd

`mmap` 修复是一个很典型的边界值问题。旧实现把 `MAP_ANONYMOUS` 和 `fd <= 0` 绑在一起，导致两个错误：

- file-backed mmap 不能使用 fd 0；
- anonymous mmap 传入正 fd 时也会被错误拒绝。

从 Linux 语义看，fd 0 完全可能是一个普通打开文件。它常见地表示 stdin，但这只是进程约定，不是内核 ABI 约束。内核实现不能因为 fd 数值是 0 就判断它不是有效文件。

修复的关键是先判断 `MAP_ANONYMOUS`：

- 如果是 anonymous mapping，Linux 会忽略 fd，但仍要求 offset 等其他参数合法；
- 如果不是 anonymous mapping，fd 0 应该按正常 fd 查找。

这个案例说明，syscall 兼容性里很多 bug 不在“大功能缺失”，而在边界值被直觉化处理。

### 4.2 典型案例：`wait4` invalid options 不能消费 child

`wait4` 的问题更隐蔽。旧实现会用 `from_bits_truncate` 静默丢弃未知 option bits，这会导致 Linux 中应返回 `EINVAL` 的参数被 StarryOS 接受。更严重的是，如果 invalid wait4 在扫描 child 后才失败，可能会错误消费 child status。

这里的修复重点不是单纯返回 `EINVAL`，而是确保：

- 在扫描或回收 child 前先拒绝非法 options；
- `WEXITED`、`WNOWAIT` 等 waitid-only 选项不能进入 wait4 路径；
- 错误路径不能改变进程状态。

这个案例让我意识到，Linux ABI 的关键不只是“成功路径输出是否一样”，还包括“失败路径是否无副作用”。很多应用依赖失败调用不会改变状态，这也是 regression 必须覆盖的内容。

### 4.3 典型案例：`linkat` 与 symlink 语义

`linkat` 旧实现对 unsupported flags 只是 warning，没有返回 `EINVAL`；同时默认路径解析会跟随最终 symlink，导致 `flags=0` 时没有按 Linux 语义硬链接 symlink 本身。

Linux 中 `linkat` 默认 no-follow，只有传入 `AT_SYMLINK_FOLLOW` 才跟随 symlink target。这个差异在普通文件测试中不明显，但构建系统、包管理器和兼容性测试会依赖。

这类问题的经验是：路径解析不是一个中立操作。是否 follow symlink、何时检查 flags、目标已存在时的 errno 优先级，都会成为用户态可观察 ABI。

### 4.4 Exp2 的共性结论

Exp2 的 8 个 PR 可以归纳成几类共性问题：

- **flags/mode 校验过晚或过宽**：非法参数应在路径解析、权限检查或状态修改前被拒绝。
- **legacy syscall 与新 syscall 参数混淆**：`faccessat`/`faccessat2`、`fchmodat`/`fchmodat2` 不能简单共用 raw 参数。
- **边界值不能按直觉处理**：fd 0、size 0、mode type 0 都有明确 Linux 行为。
- **错误路径不能有副作用**：invalid wait4 不能回收 child，invalid chmod flags 不能真的 chmod。

这些经验后来在 BusyBox 和 Codex CLI 实验中继续出现，只是入口从“最小 syscall 测例”变成了“真实应用失败”。

## 五、Exp3：从 BusyBox 应用失败反推内核兼容缺口

Exp3 把 Exp2 的方法推进到真实应用层。入口不再是某个 syscall 名称，而是 BusyBox issue backlog 中的失败项。Developer 每轮需要选择一个 applet，记录 Linux 上的命令和 oracle，再在 StarryOS 中复现失败，最后追到底层兼容缺口。

最终形成 7 个已合入 PR：

| PR | BusyBox 目标 | 主要缺口 |
| --- | --- | --- |
| [#477](https://github.com/rcore-os/tgoskits/pull/477) | `nice` | priority syscall |
| [#479](https://github.com/rcore-os/tgoskits/pull/479) | `ip link` | route netlink `RTM_GETLINK` |
| [#480](https://github.com/rcore-os/tgoskits/pull/480) | `arp` | `/proc/net/arp` |
| [#481](https://github.com/rcore-os/tgoskits/pull/481) | `ip addr` | route netlink `RTM_GETADDR` |
| [#482](https://github.com/rcore-os/tgoskits/pull/482) | `pidof` | `/proc/<pid>` 中用户可见 PID |
| [#483](https://github.com/rcore-os/tgoskits/pull/483) | `iplink` | standalone applet 的 link dump 覆盖 |
| [#484](https://github.com/rcore-os/tgoskits/pull/484) | `arping` | `AF_PACKET` 与 ARP synthetic reply |

### 5.1 典型案例：`ip link` 和 `ip addr` 背后的 route netlink

BusyBox `ip link` 和 `ip addr` 表面上只是打印网络接口信息，但它们并不只是读一个普通文件，而是通过 `NETLINK_ROUTE` socket 与内核交换 rtnetlink 消息。

`ip link` 需要：

- 接收 `RTM_GETLINK` 请求；
- 返回 `RTM_NEWLINK` records；
- 合成 loopback/eth0 的接口名、MTU、MAC、operstate 等 attributes。

`ip addr` 需要：

- 接收 `RTM_GETADDR` 请求；
- 返回 `RTM_NEWADDR` records；
- 合成 loopback/eth0 的 IPv4 地址属性。

这个案例说明，真实 Linux 应用依赖的是“协议 ABI”，不是一个 syscall 是否存在。StarryOS 可以先不实现完整 Linux 网络栈，但常见查询路径必须返回格式正确、字段足够的最小响应。

### 5.2 典型案例：`/proc/net/arp` 是文本 ABI

BusyBox `arp` 依赖 `/proc/net/arp`。如果该节点不存在，或者 header/字段格式不符合 Linux，应用就无法解析。

这里的关键不是“内核是否真的维护完整 ARP cache”，而是：

- `/proc/net/arp` 路径要存在；
- 输出要有 Linux 兼容 header；
- 字段顺序、设备名、硬件地址等要可被 BusyBox 解析。

这说明 `/proc` 不是调试信息，而是大量用户态工具依赖的稳定接口。它的文本格式本身就是 ABI。

### 5.3 典型案例：`pidof` 暴露内部 task id 与用户 PID 混用

BusyBox `pidof` 依赖 `/proc/<pid>` 枚举，并期望 init 可见为 PID 1。StarryOS 旧实现中部分 procfs 路径暴露的是内部 task/thread id，而不是用户可见 process id，导致 `pidof` 找不到预期目标。

这个问题的本质是：内核内部对象标识和 Linux 用户态 ABI 标识不能混用。进程管理工具、shell、服务脚本看到的是 process id，而不是内核调度器内部的 task id。类似问题会影响 `/proc`、`kill`、`wait`、`getpgid`、`getsid` 等多个接口。

### 5.4 典型案例：`arping` 需要协议字段正确，而不只是 stdout 通过

`busybox arping` 会打开 `AF_PACKET` socket，绑定接口，发送 ARP request，再等待 ARP reply。为了让它通过，StarryOS 需要提供最小 packet socket、接口查询、bind/getsockname、packet send/recv 和 synthetic ARP reply。

这个 PR 的 review 过程很有价值：如果只看 BusyBox 输出包含 `Received`，可能会接受一个字段不严谨的 synthetic reply。但协议类修复必须检查 SHA/SPA/THA/TPA 等关键字段，否则测试就是假阳性。

这个案例体现了 Exp3 的一个原则：应用级 oracle 可以发现问题，但源码级 regression 仍要检查底层语义。

### 5.5 Exp3 的共性结论

BusyBox 实验说明，应用兼容性不是“实现更多 syscall 名字”这么简单。真实工具经常组合依赖：

- procfs 文本节点；
- route netlink message；
- packet socket；
- ioctl；
- 用户可见 PID；
- 进程优先级 syscall；
- shell 脚本对 stdout/stderr/errno 的解析。

Exp3 的价值在于把应用失败稳定地拆回这些底层接口，再形成小 PR。这也为 Exp4 运行 Codex CLI 打下基础，因为 Codex/git/curl/rg 这类真实工具同样会综合压力测试文件系统、网络、进程、多路复用和用户内存访问。

## 六、Exp4：让 Codex CLI 在 StarryOS 中运行

Exp4 是整个 Big Lab B 最后也是最综合的一步。目标不是只让 `codex --version` 打印出来，而是让 Codex CLI 在 StarryOS x86_64 QEMU guest 中完成基础 coding-agent 工作流：

- 启动 Linux x86_64 musl 版本 Codex CLI；
- 运行 `codex --help`、`codex exec --help`、`codex login status`；
- 注入 `auth.json`、CA 证书和代理后在线请求模型；
- 在 guest 内使用 `git`、`rg`、shell 命令读写文件；
- 在 StarryOS guest 内 clone TGOSKits 仓库，让运行在 StarryOS 上的 Codex CLI 阅读、修改和验证 StarryOS/TGOSKits 代码，形成最小 self-evolve 闭环。

这里 Codex CLI 既是实验目标，也是压力测试工具。它比 BusyBox 更复杂：多线程、网络请求、TLS、git、子进程、poll/epoll、zombie 进程、cwd、TCP sockopts 都会被真实触发。

### 6.1 分阶段实施计划

真实接入过程比最终 demo 更复杂。整个 Exp4 基本按阶段 A 到 I 推进，每一步只放大一个维度，并把新暴露的问题拆成可复现的 StarryOS bugfix。

1. **阶段 A：基线环境和可观测性**  
   先不碰 Codex，确认 x86_64 StarryOS QEMU、Alpine rootfs、DNS、HTTPS、CA、`/tmp`、`/root`、`/dev/null`、`/dev/urandom`、`/proc/self/exe` 等前置条件可用。同时准备本地 QEMU、Codex 专用 rootfs、logs 和 qemu config 目录。这个阶段的目标是保证后续失败不是因为基础环境不可观测。

2. **阶段 B：注入 Codex 二进制并跑离线 smoke**  
   把 `@openai/codex@0.115.0-linux-x64` 包里的 Codex musl 二进制和 `rg` 注入专用 rootfs，先只验证 `codex --version`、`codex --help`、`codex exec --help`、`codex login status`、`rg --version`。这一阶段证明 Codex 作为普通 Linux x86_64 用户程序可以在 StarryOS 中启动，暂时不引入网络和模型请求。

3. **阶段 C：接入 auth、TLS、HTTPS 和最小在线请求**  
   注入 `auth.json`、CA 证书和代理配置，先用 `curl` 验证 DNS/TLS/HTTPS，再运行最小 `codex exec`。这一步首次触发真实异步网络请求，并暴露 `epoll_wait` 跨阻塞点持有用户 `events` buffer 的 page fault。修复后补 `bug-epoll-wait-user-buffer-race` 回归。

4. **阶段 D：小 workspace 读写、`rg` 和 shell 闭环**  
   让 Codex 在一个很小的 workspace 中读 `README.md` / `AGENTS.md`，修改文件，运行 `rg` 和 shell 命令，再由 guest 侧读回验证。这个阶段暴露了 `poll/ppoll` 等待期间持有用户 `pollfd` 引用的问题，修复后补 `bug-poll-wait-user-buffer-race` 回归。

5. **阶段 E：扩大到 TGOSKits 源码子集**  
   不是一开始就在 guest 内 clone 全仓，而是先把精简 TGOSKits 源码子集注入 rootfs，覆盖 `AGENTS.md`、StarryOS kernel、test-suit 和 axbuild 相关路径。Codex 以 `/root/tgoskits` 为工作目录完成只读理解、`rg` 搜索和受控写文件。这里发现了 Git 仓库发现、输出过长和开放式 prompt 不稳定等问题，因此改用固定脚本和 marker 约束任务。

6. **阶段 F：按 Codex 日志补齐兼容性缺口**  
   不盲目实现所有可能能力，而是只修阶段 B-E 真实日志中已经触发的问题：`PR_CAPBSET_READ` warning、raw `getcwd` ABI、TCP keepalive/user-timeout sockopts、未回收 zombie 的 `getpgid`/`getsid`/`kill` 可见性等。每个问题都补最小 C regression，再用 Codex smoke 证明原始路径不再失败。

7. **阶段 G：把无 secret 的 smoke 工程化**  
   早期方案是把 `codex-help` 放入 StarryOS test-suit，并设计 `prebuilt-assets` pipeline 注入 Codex/rg 这类本地预构建资产。后续根据 review 意见，考虑到下载 Codex 会增加 CI 耗时和自部署主机流量，最终把公开形态调整为 `examples/starry/codex-cli` 下的 opt-in example，不进入默认 CI。

8. **阶段 H：在 TGOSKits 子任务上试跑 Codex**  
   让 guest 内 Codex 在 TGOSKits 源码子集上完成一个只读理解任务和一个受控写文件任务，并用 guest 内 `grep`、`git status`、`git diff` 验收。这里还处理了源码 tar 解压后 owner 不一致导致 Git `dubious ownership` 的问题，通过 `chown -R root:root` 恢复普通 git workflow。

9. **阶段 I：演示整理和 failure-path 清理**  
   把 Stage H 的 prompt、Codex 最终回答、生成文件内容、`git status` 和 `git diff` 分段打印，方便现场展示。同时调查 `apply_patch` 非阻塞噪声，并修复 tmpfs/pseudofs cwd cleanup failure-path panic，补 `bug-tmpfs-cwd-drop-safe` 回归。

在这些阶段完成后，最终 demo 才进一步演进为在 StarryOS guest 内直接 clone TGOSKits `dev` 分支，并让 Codex CLI 执行 syscall/kernel bug-hunt。这样展示的不是“host 上的 Codex 帮 StarryOS 写代码”，而是“StarryOS 自己运行 Codex，Codex 再阅读、修改和验证 StarryOS/TGOSKits 代码”的最小 self-evolve 闭环。

### 6.2 环境建立

最终形成的 opt-in example 位于：

```text
examples/starry/codex-cli/
```

核心脚本包括：

- `prepare_codex_assets.sh`：准备本地 Codex CLI 和 `rg` 资产。
- `prepare_codex_rootfs.sh`：基于 Alpine rootfs 注入 Codex、rg、git/CA/auth/proxy 等内容，生成 ignored rootfs。

离线 rootfs 默认路径：

```text
tmp/axbuild/rootfs/rootfs-x86_64-codex.img
```

在线 rootfs 示例路径：

```text
tmp/axbuild/rootfs/rootfs-x86_64-codex-online.img
```

在线 demo 中需要注入：

- host 侧 `target/auth.json`；
- CA bundle；
- 代理配置，例如本地演示使用 `http://172.31.48.1:7890`；
- guest 内 `/root/.codex/auth.json` 和 `/root/.codex/starry-online-env`。

### 6.3 典型案例：`epoll_wait` 持有用户缓冲区引用导致 page fault

Codex 在线请求模型时暴露了一个很关键的问题：`epoll_wait` 在阻塞等待期间持有用户态 `events` buffer 的内核引用。多线程程序在等待期间可能由另一个线程 `munmap` 或修改该 buffer 的页权限。唤醒后内核继续写旧用户地址，就会触发 kernel page fault。

Linux 的处理方式不是跨阻塞点保留用户引用，而是在内核侧维护临时 event buffer，返回用户态前再逐项 copy_to_user。用户缓冲区失效时返回 `EFAULT`，不能让内核 panic。

这个问题最终拆入 PR #523。它的意义不只是修 Codex，而是修掉了一类用户指针 TOCTOU 问题。`poll/ppoll` 也有类似问题：入口应 copy-in 到内核副本，等待期间只改内核数据，返回时 checked copy-out。

### 6.4 典型案例：raw `getcwd` 返回值和错误优先级

`git` 路径暴露了 raw `getcwd` ABI 问题。Linux raw syscall 成功时返回包含末尾 NUL 的长度；小 buffer 返回 `ERANGE`；只有 buffer 足够大但用户指针无效时才返回 `EFAULT`。

旧实现的问题包括：

- 返回长度不含 NUL；
- null buffer 过早返回 `EFAULT`；
- 没有匹配 Linux 的错误优先级。

Review 中还专门指出 `NULL + too-small size` 应先返回 `ERANGE`。这个细节说明 ABI 修复不能只覆盖“常见成功路径”，还要覆盖参数组合下的错误优先级。

### 6.5 典型案例：TCP keepalive 不能只让 setsockopt 成功

Codex/curl/hyper 会设置 `TCP_KEEPIDLE`、`TCP_KEEPINTVL`、`TCP_KEEPCNT`、`TCP_USER_TIMEOUT`。最初如果只是让 `setsockopt` 返回成功，但不保存状态、不支持 `getsockopt` 读回，应用仍然能观察到差异。

Review 进一步指出，`TCP_KEEPIDLE` 不仅要保存，还要真正接入 smol socket 的 keepalive idle timeout。否则应用设置 300 秒，但底层仍使用默认 75 秒，就可能导致过早断开。

这个案例代表了一类“伪兼容”风险：返回成功并不等于兼容。只要应用能通过后续 `getsockopt` 或网络行为观察到差异，就必须把状态和实际行为接上。

### 6.6 典型案例：zombie 进程在 wait 前仍应可见

Codex 执行任务会创建和清理子进程。旧实现中，未被 `waitpid` 回收的 zombie child 在某些查询路径中过早不可见，导致 `getpgid`、`getsid`、`kill(pid, 0)`、`kill(pid, SIGKILL)` 行为与 Linux 不一致。

Linux 中 zombie 虽然已经退出，但在 parent wait 前仍保留进程表可见性和权限判断所需信息。修复思路是保留 unreaped zombie 的 `Process` 和凭据快照，让查询和信号权限检查在 wait 前仍能找到它。

这个问题把 Exp2/Exp3 的经验连了起来：进程状态不是简单 alive/dead 二值，用户可见生命周期必须与 Linux 匹配。

### 6.7 典型案例：tmpfs cwd cleanup failure-path panic

另一个与 Codex 路径相关但更偏 failure-path 的问题是 tmpfs cwd cleanup。子进程 cwd 指向 tmpfs 目录时，父进程先 `rmdir`，子进程退出清理路径可能在 atomic context 中获取睡眠锁并 panic。

修复的方向是把 cleanup 从 drop/failure path 挪回正常 syscall 上下文：

- `MemoryNode::drop` 不再清理目录 entries；
- tmpfs inode slab/metadata 使用适合上下文的锁；
- `unlink`/`rename` 在正常 syscall 上下文提前清理被移除目录项。

这个案例说明，内核代码不仅要关心主路径，还要关心资源回收路径的锁语义和执行上下文。

### 6.8 Exp4 的 PR 拆分

Exp4 最终按中等粒度拆分，而不是把所有 Codex 相关改动塞进一个大 PR：

| PR | 主题 | 说明 |
| --- | --- | --- |
| [#523](https://github.com/rcore-os/tgoskits/pull/523) | `poll` / `epoll_wait` 用户缓冲区安全 | 修复阻塞期间用户 buffer 被 unmap 后 kernel page fault |
| [#524](https://github.com/rcore-os/tgoskits/pull/524) | CLI 兼容缺口 | 覆盖 `getcwd`、`PR_CAPBSET_READ`、TCP keepalive、zombie 可见性 |
| [#525](https://github.com/rcore-os/tgoskits/pull/525) | tmpfs cwd cleanup panic | 修复 failure/drop path panic |
| [#575](https://github.com/rcore-os/tgoskits/pull/575) | Codex CLI example flow | 按 review 调整为 `examples/starry/codex-cli` opt-in example，不进入 CI |

这个拆分方式有两个好处：

1. reviewer 可以逐个对照 Linux 语义和 regression；
2. Codex demo/example 不依赖把 auth、rootfs、大二进制或在线网络请求放进 CI。

## 七、整体方法论总结

### 7.1 从“能跑”到“语义正确”

这次实验中反复出现一个教训：让测试表面通过并不难，真正难的是确认行为与 Linux 可观察语义一致。

例如：

- `setsockopt` 返回 0，但 `getsockopt` 读不回，仍然不兼容；
- BusyBox `arping` 输出 `Received`，但 ARP reply 字段不完整，仍然不可靠；
- `getcwd(NULL, small_size)` 如果返回 `EFAULT` 而不是 `ERANGE`，仍然不符合 Linux 错误优先级；
- invalid `wait4` 如果返回错误前消费 child status，就是严重状态副作用。

因此每个修复都应回答三个问题：

1. Linux 在这个输入下到底做什么？
2. StarryOS 当前差异是什么？
3. 修复后有没有长期 regression 固化这个差异？

### 7.2 应用兼容性最终会落到很多小 ABI

BusyBox 和 Codex CLI 都说明，大应用失败通常不是因为“缺一个大功能”，而是很多小 ABI 组合不完整：

- fd 边界值；
- flags/mode 校验；
- symlink follow/no-follow；
- errno 优先级；
- `/proc` 文本格式；
- netlink attributes；
- packet socket sockaddr；
- poll/epoll 用户指针；
- zombie 生命周期；
- TCP socket option 状态。

这些问题单独看都很小，但真实应用会同时触发它们。StarryOS 的兼容性提升，实际就是不断把这些小可观察差异补齐。

### 7.3 Pipeline 的关键不是自动写代码，而是自动维持纪律

Exp1 pipeline 最有价值的地方不是“让模型自由发挥”，而是把工程纪律固定下来：

- 单轮只处理一个小目标；
- Linux 行为必须作为基准；
- 必须有 StarryOS 差分；
- 必须有长期测试；
- Reviewer 可以否决；
- PASS 后才提交；
- journal 记录历史，避免重复。

如果没有这些约束，模型很容易一次改很多文件、把多个问题混在一起、只修当前输出、不补 regression，或者重复处理已经完成的目标。

### 7.4 PR 粒度要服务 review

这次 PR 拆分的经验是：粒度太小会增加管理成本，粒度太大会让 reviewer 难以判断语义是否正确。比较合适的是“一个明确 bug 或一组强相关兼容缺口”。

例如 Exp4 中：

- `poll/epoll` 用户缓冲区 race 是一类问题，适合一个 PR；
- `getcwd`、`prctl`、TCP keepalive、zombie 可见性都由 Codex/git/curl CLI 路径暴露，且都属于基础 Linux CLI 兼容缺口，可以合成一个中等粒度 PR，但 PR 描述必须分小节写清楚；
- tmpfs cwd cleanup 是独立 failure-path panic，应该单独 PR；
- Codex example/demo 不应和内核 bugfix 混在一起。

### 7.5 Codex CLI 既是工具，也是测试负载

Exp4 最有意思的地方是：Codex 不只是帮助写代码的工具，它本身也是一个复杂 Linux 应用。为了让它在 StarryOS 中运行，系统必须同时满足：

- ELF/动态运行环境；
- 文件系统和 cwd；
- 网络、TLS、代理；
- TCP socket options；
- poll/epoll；
- 子进程和 zombie；
- git/rg 等工具链；
- 用户态 buffer 的安全 copy-in/copy-out。

因此运行 Codex CLI 的价值不只是演示，而是给 StarryOS 提供了一个高强度、真实、多线程、多进程、联网的兼容性测试负载。

## 八、最终成果

Big Lab B 最终形成了一个由浅入深的成果链：

1. **Task1 热身**  
   熟悉 ArceOS/TGOSKits 的构建、运行、VFS、mmap、分配器和 `no_std` 环境。

2. **Exp1 Pipeline**  
   建立 Codex-only 自动迭代闭环，把模型能力和确定性脚本分工固定下来。

3. **Exp2 Syscall**  
   产出 8 个已合入 syscall 兼容性 PR，覆盖 flags、errno、边界值、legacy dispatch 和错误路径副作用。

4. **Exp3 BusyBox**  
   产出 7 个已合入 BusyBox 兼容性 PR，覆盖 procfs、route netlink、packet socket、PID 可见性和 priority syscall。

5. **Exp4 Codex on StarryOS**  
   修复 Codex CLI 路径暴露的多处 Linux ABI/failure-path 问题，完成 x86_64 QEMU 中的 Codex CLI offline/online demo，并整理 opt-in example。

整体上，这次 Big Lab B 的结果可以概括为：

```text
用 Codex 建立自动化兼容性改进流程，
用 Linux 基准和 StarryOS 回归测试约束修复质量，
用 BusyBox 和 Codex CLI 这类真实应用压力测试系统边界，
最终把发现的问题拆成可 review、可合入、可长期维护的小修复。
```

## 九、个人总结与反思

这组实验让我对操作系统兼容性有了更具体的认识。类 Linux OS 的难点不只是实现 syscall 表，而是实现大量用户态可以观察到的细节。很多 bug 看起来很小，例如一个 flag、一个 errno、一个 buffer size、一个 `/proc` 字段，但它们会决定真实应用能否运行。

另一方面，Codex pipeline 的实验也说明，AI 更适合被放进有边界的工程流程中，而不是无约束地“自动开发”。当 prompt、schema、journal、Reviewer、测试命令和 PR 粒度都被明确以后，模型可以持续承担代码阅读、差分分析、修复建议和测试补充工作；而提交、记录、分支管理和质量门禁仍由确定性脚本和人工 review 把关。

最后，Exp4 中让 Codex CLI 在 StarryOS 里运行，是一个很有象征意义的闭环：前面用 Codex 帮助改进 StarryOS，最后又把 Codex 放回 StarryOS 中，让它继续阅读 TGOSKits、寻找 syscall/内核语义问题。这还不是完整 self-evolving OS，但已经证明了最小可行路径：系统能够承载一个 coding-agent，coding-agent 又能反过来帮助系统发现和修复兼容性问题。
