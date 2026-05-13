# Exp3 StarryOS BusyBox 兼容性报告

日期：2026-05-12

## 1. 实验目标

Exp3 的目标是在 Exp1 pipeline 和 Exp2 syscall 兼容性经验的基础上，进一步面向真实 Linux 小应用集合 BusyBox 做应用级兼容性修复。

相比 Exp2 直接从 syscall 语义点出发，Exp3 的入口更偏应用：

1. 从 `linux-compatible-testsuit` issue #13 中选择一个 BusyBox `FAIL 测试`。
2. 记录该行的 `测试命令` 和 `验证方式`，把它作为本轮 oracle。
3. 在 Linux 上建立 BusyBox 行为基准。
4. 在 StarryOS riscv64 QEMU 中复现同一命令的失败。
5. 追到底层 syscall、procfs、netlink、packet socket 或进程语义缺口。
6. 做最小修复，并沉淀 BusyBox 脚本回归和源码级 C 回归。
7. 由 Reviewer 复核后拆成小 PR 提交上游。

这使得 Exp3 不只是“实现某个 syscall”，而是围绕真实 applet 的可观察行为补齐 StarryOS 的 Linux 兼容面。

## 2. Pipeline 在 Exp3 中的处理

Exp3 使用的 pipeline 仍然是 Syncer / Developer / Reviewer / Committer 闭环，但 prompt 和策略针对 BusyBox 做了调整：

- Backlog 来自 issue #13 的确认失败项。
- Developer 必须从当前未 PASS 的 BusyBox 项中选题，不能重复解决 journal/passed commits 已记录的目标。
- 每轮必须记录 issue 行的 `FAIL 测试`、`测试命令`、`验证方式`。
- Linux 行为是基准，StarryOS 失败必须能被同一 oracle 或明确等价 oracle 捕获。
- 修复后只恢复本轮目标对应的 BusyBox 检查，不能把其他 pending 分支里的检查项一起搬进来。
- confirmed bug 优先再抽取成单一源码级 C regression，通常放进 `test-suit/starryos/normal/qemu-smp1/bugfix/`。
- Reviewer 有权因为证据不足、语义不完整、测试误弱化或引入非目标回归而返回 `REVISE`。

这个流程的价值在于把应用失败拆回内核兼容性问题：BusyBox 表面上是一个 applet 失败，根因往往是 procfs 节点缺失、netlink 消息不完整、packet socket 未实现、PID 可见语义不对，或调度优先级 syscall 不兼容。

## 3. PR 总览

Exp3 最终形成 7 个已合入 PR：

| PR | 标题 | BusyBox 目标 |
| --- | --- | --- |
| [#477](https://github.com/rcore-os/tgoskits/pull/477) | `fix(sched): support busybox nice priority syscalls` | `busybox_nice` |
| [#479](https://github.com/rcore-os/tgoskits/pull/479) | `fix(netlink): support busybox ip link` | `busybox_ip` / `ip link` |
| [#480](https://github.com/rcore-os/tgoskits/pull/480) | `fix(proc): expose arp table for busybox arp` | `busybox_arp` |
| [#481](https://github.com/rcore-os/tgoskits/pull/481) | `fix(netlink): support busybox ip addr` | `busybox_ipaddr` |
| [#482](https://github.com/rcore-os/tgoskits/pull/482) | `fix(proc): expose init pid for busybox pidof` | `busybox_pidof` |
| [#483](https://github.com/rcore-os/tgoskits/pull/483) | `fix(netlink): support busybox iplink` | `busybox_iplink` |
| [#484](https://github.com/rcore-os/tgoskits/pull/484) | `fix(packet): support busybox arping` | `busybox_arping` |

这 7 个 PR 覆盖了进程优先级、procfs、route netlink、packet socket 和 BusyBox 脚本回归，是从“应用失败”反推“内核兼容缺口”的一组代表性样例。

## 4. 各 PR 问题与分析

### 4.1 PR #477：支持 BusyBox `nice`

链接：<https://github.com/rcore-os/tgoskits/pull/477>

BusyBox 目标：`busybox nice -n 10 busybox echo nice_ok`

问题：BusyBox `nice` 依赖 `setpriority(PRIO_PROCESS, 0, prio)` 改变当前进程 nice 值，并依赖 `getpriority` 按 Linux raw priority 语义返回。StarryOS 之前没有完整维护当前任务的 priority 语义，导致 `nice` 路径不能稳定执行。

根因：`getpriority`/`setpriority` 对 `PRIO_PROCESS` 当前进程路径、无效 `which`、不存在目标、task creation 后 priority state 继承等语义不够接近 Linux。

修复：补齐当前进程路径的 `getpriority`/`setpriority` 兼容处理；保留 priority 状态；对无效 `which` 和缺失 process target 返回 Linux 风格错误。

测试：新增源码级回归 `bug-setpriority-current`，并在 BusyBox 脚本中加入 `busybox nice -n 10 busybox echo nice_ok` smoke。

分析：这个 PR 说明 BusyBox 的一个普通 applet 往往依赖底层 process/scheduler ABI。即使 StarryOS 调度器不一定完整实现 Linux nice 调度效果，也需要先保证用户可见 syscall 状态与错误码兼容。

### 4.2 PR #479：支持 BusyBox `ip link`

链接：<https://github.com/rcore-os/tgoskits/pull/479>

BusyBox 目标：`busybox ip link 2>&1`，oracle 是输出包含 `link/`。

问题：BusyBox `ip link` 会打开 `NETLINK_ROUTE` socket，发送 `RTM_GETLINK` dump request，期望收到接口元数据。StarryOS 原有 netlink 支持主要覆盖 uevent，不能返回 route netlink link records。

根因：netlink socket 没有把 `NETLINK_ROUTE` 的 `sendmsg`/`recvmsg` 组织成 `RTM_GETLINK` response path，也缺少 loopback/eth0 的合成 link metadata。

修复：扩展 route netlink，支持 `RTM_GETLINK` dump；返回 synthetic loopback 和 `eth0` link metadata，包括接口名、硬件地址、MTU、qdisc、tx queue length、operstate 等属性。

测试：新增源码级 `bug-netlink-getlink`，并恢复 BusyBox `ip link` 检查。

分析：这是 Exp3 的核心类型之一：BusyBox 不是直接读某个 syscall 返回值，而是依赖 Linux netlink 协议。StarryOS 不需要一开始完整实现所有 rtnetlink，但需要给常见查询路径提供稳定、格式正确的最小响应。

### 4.3 PR #480：为 BusyBox `arp` 暴露 `/proc/net/arp`

链接：<https://github.com/rcore-os/tgoskits/pull/480>

BusyBox 目标：`busybox arp 2>&1`，oracle 是输出包含 `HWtype` 或 `[ether]`。

问题：BusyBox `arp` 读取 `/proc/net/arp` 并解析 Linux ARP table header 和条目。StarryOS 没有暴露该 procfs 节点，导致 applet 无法渲染 ARP 表。

根因：procfs 网络信息节点不足，缺少 `/proc/net/arp` 的 Linux 兼容文件内容；同时需要从网络设备层拿到可展示的接口信息。

修复：在 procfs 中增加最小 `/proc/net/arp`，输出 Linux 兼容 header 和 `eth0` synthetic ARP entry；相关网络设备信息通过 axnet-ng 路径暴露。

测试：新增源码级 `bug-proc-net-arp`，验证 ARP table 字段；BusyBox 脚本加入 `busybox arp` 检查。

分析：这个 PR 体现了 procfs 对 Linux 应用兼容的重要性。很多工具不走专门 syscall，而是读取 `/proc` 里的文本接口；这些文本字段名、列顺序和内容格式本身就是 ABI。

### 4.4 PR #481：支持 BusyBox `ip addr`

链接：<https://github.com/rcore-os/tgoskits/pull/481>

BusyBox 目标：`busybox ip addr 2>&1`，oracle 是输出包含 `inet `。

问题：BusyBox `ip addr` 通过 `NETLINK_ROUTE` 请求地址 dump，发送 `RTM_GETADDR` 后期望收到 `RTM_NEWADDR` 地址记录。StarryOS 没有合成 route netlink address records，所以无法显示 IPv4 地址。

根因：route netlink 只覆盖了更有限的路径，缺少 `RTM_GETADDR` -> `RTM_NEWADDR` 的响应，以及 loopback/eth0 的 IPv4 address attributes。

修复：扩展 route netlink，支持 `RTM_GETADDR` responses；返回 loopback 和 `eth0` IPv4 地址，同时保留 `RTM_GETLINK` metadata 以满足地址 listing 中对接口信息的依赖。

测试：新增源码级 `bug-netlink-getaddr`，BusyBox 脚本加入 `busybox ip addr` 检查。

分析：`ip link` 和 `ip addr` 看起来相近，但实际 netlink message 类型不同。拆成独立 PR 的好处是 reviewer 可以分别检查 link metadata 和 address records 的格式与 oracle。

### 4.5 PR #482：为 BusyBox `pidof` 暴露 init PID

链接：<https://github.com/rcore-os/tgoskits/pull/482>

BusyBox 目标：`busybox pidof -s init 2>&1 || busybox pidof -s sh 2>&1`，oracle 是输出包含 `1`。

问题：BusyBox `pidof` 依赖 Linux 风格 `/proc/<pid>` 进程目录，尤其是 init 通常可见为 PID 1。StarryOS 之前在 procfs 某些路径暴露的是 task/thread id，而不是用户可见 process id，导致 `pidof` 找不到预期 init PID。

根因：内部 task id 和 Linux 用户态 process id 混用；procfs 根目录枚举和解析没有完全按 process id 展示。

修复：让 init process 通过 `/proc` 暴露为 PID 1，同时保留内部真实 task id；`/proc/<pid>` 按 process id 枚举和查找。

测试：新增源码级 `bug-proc-init-pid`，BusyBox 脚本加入 `pidof` 检查。

分析：这类问题对 shell 和进程管理工具影响很大。Linux 兼容层需要清楚区分 kernel 内部 task/thread 标识和用户态可见 PID，否则 `/proc`、`kill`、`wait`、`pidof` 等都会出现连锁问题。

### 4.6 PR #483：支持 standalone BusyBox `iplink`

链接：<https://github.com/rcore-os/tgoskits/pull/483>

BusyBox 目标：`busybox iplink 2>&1`，oracle 是输出包含 `link/`。

问题：BusyBox 有 standalone `iplink` applet，它走的也是 route netlink `RTM_GETLINK` 查询路径。StarryOS 缺少对应的 link dump response 时，该 applet 不能输出 `link/` 行。

根因：与 `ip link` 同源，都是 `NETLINK_ROUTE` / `RTM_GETLINK` link records 不完整。即使一个 BusyBox 命令形式通过了，也需要把 standalone applet 的脚本入口单独纳入回归，防止漏测。

修复：补齐/复用 `RTM_GETLINK` synthetic link metadata，并将 standalone `busybox iplink` 加入测试覆盖。

测试：源码级 `bug-netlink-getlink` 覆盖 route netlink link dump，BusyBox 脚本加入 `busybox iplink` 检查。

分析：这个 PR 的意义不仅是功能补丁，也是测试覆盖补丁。BusyBox 同一能力可能有 `ip link` 和 `iplink` 两种入口，pipeline 要按 issue backlog 的具体 FAIL 测试逐项恢复，而不能因为底层根因相同就跳过应用级 oracle。

### 4.7 PR #484：支持 BusyBox `arping`

链接：<https://github.com/rcore-os/tgoskits/pull/484>

BusyBox 目标：`busybox arping -c 1 127.0.0.1 2>&1`，oracle 是输出包含 `Received`。

问题：BusyBox `arping` 会打开 `AF_PACKET` socket，查询接口元数据，绑定 `eth0`，发送 ARP request，然后等待 reply。StarryOS 没有实现该 packet socket 表面，导致 `arping` 无法收到 reply。

根因：缺少 `AF_PACKET` datagram socket、`sockaddr_ll` bind/getsockname、interface ioctl、packet send/recv，以及 ARP request 的 synthetic reply 路径。

修复：增加最小 `AF_PACKET` datagram socket；支持 `eth0` packet binding、基本接口 ioctl、`getsockname`；对 BusyBox 发送的 ARP request 合成 ARP reply。

测试：新增源码级 `bug-packet-arping`，并在 BusyBox 脚本中加入 `busybox arping -c 1 ...` 检查。

分析：这是 Exp3 中最接近“应用驱动协议栈补洞”的 PR。Reviewer 曾要求修正 synthetic ARP reply 的字段构造，说明仅让 applet oracle 通过还不够，源码级回归必须检查 SHA/SPA/THA/TPA 等关键协议字段，避免假阳性。

## 5. 共性分析

Exp3 的 7 个 PR 可以归纳为四类能力补齐。

第一类是 **进程/调度可见语义**。`busybox_nice` 和 `busybox_pidof` 分别暴露了 priority syscall 与用户可见 PID/procfs 的问题。这类问题不一定影响简单程序启动，但会影响脚本、服务管理和进程工具。

第二类是 **procfs 文本 ABI**。`busybox_arp` 说明 `/proc` 文件本身就是 Linux ABI。工具可能不调用复杂 syscall，而是依赖文本节点的 header、字段和行格式。

第三类是 **route netlink ABI**。`busybox ip link`、`ip addr`、`iplink` 都依赖 `NETLINK_ROUTE`，但具体 message 类型、attributes 和 applet 入口不同。StarryOS 可以先合成 loopback/eth0 的最小稳定响应，逐步覆盖常见工具路径。

第四类是 **packet socket 与协议行为**。`busybox arping` 需要 `AF_PACKET`、interface metadata 和 ARP reply。它比纯 procfs/netlink 更接近网络协议路径，因此必须用源码级测试检查协议字段，而不仅看 BusyBox stdout。

## 6. 验证方式

Exp3 的验证通常包括三层：

1. **Linux BusyBox 基准**  
   使用 issue #13 的 `测试命令` 和 `验证方式`，确认 Linux BusyBox 输出和 oracle。

2. **StarryOS 源码级回归**  
   每个 confirmed bug 尽量新增一个 C regression，例如：
   - `bug-setpriority-current`
   - `bug-netlink-getlink`
   - `bug-proc-net-arp`
   - `bug-netlink-getaddr`
   - `bug-proc-init-pid`
   - `bug-packet-arping`

3. **BusyBox 应用级回归**  
   在 `test-suit/starryos/normal/qemu-smp1/busybox/sh/busybox-tests.sh` 中恢复本轮目标 applet 的检查，并运行 StarryOS QEMU busybox case。

常见验证命令包括：

```bash
git diff --check
cargo xtask clippy --package starry-kernel
cargo xtask starry test qemu --arch riscv64 --test-group normal --test-case bugfix
cargo xtask starry test qemu --arch riscv64 --test-group normal --test-case busybox
```

部分 PR 后续补了 aarch64、x86_64、loongarch64 的 QEMU 配置或回归入口，说明这些 BusyBox 修复逐步从 riscv64 first-pass 扩展到多架构覆盖。

## 7. 与 Exp2 / Exp4 的关系

Exp2 证明了 pipeline 可以发现 syscall 细节差异；Exp3 则把这个方法迁移到应用兼容性场景。BusyBox 失败往往不是单一 syscall 返回值问题，而是多个 Linux 可观察接口组合不足：

- `ip` 系列依赖 route netlink；
- `arp` 依赖 `/proc/net/arp`；
- `pidof` 依赖 `/proc/<pid>`；
- `arping` 依赖 packet socket 和 ARP 行为；
- `nice` 依赖 process priority syscall。

这些经验直接影响 Exp4 Codex CLI：真实应用会同时压力测试文件系统、procfs、网络、进程和多路复用接口。Exp3 的主要价值是证明 pipeline 可以从“应用失败”稳定收敛到底层 Linux ABI 缺口，再把修复拆成 reviewer 能接受的小 PR。

## 8. 总结

Exp3 的核心成果是：基于 Codex pipeline 和 BusyBox issue backlog，完成了 7 个已合入的 BusyBox 兼容性 PR。

这些 PR 覆盖了 `nice`、`ip link`、`arp`、`ip addr`、`pidof`、`iplink`、`arping` 等典型 applet。它们共同说明，StarryOS 的应用兼容性提升不能只靠“补 syscall 名字”，还需要补齐 procfs 文本 ABI、route netlink 消息、packet socket、PID 可见性和错误路径语义。Exp3 把 Exp2 的 syscall 差分方法推进到了真实应用层，并为后续运行 Codex CLI 这类更复杂用户程序积累了经验。
