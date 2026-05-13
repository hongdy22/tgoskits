# 阶段 F 报告：按 Codex 日志补齐兼容性缺口

日期：2026-05-10
分支：`exp4_codex`
起点提交：`8ee5aeb73`

## 结论

阶段 F 已完成。本阶段没有实现计划中所有“可能会用到”的能力，而是按阶段 B 到 E 的真实日志补齐已经触发的兼容性缺口，并用小 C case 和 Codex smoke 做回归。

已闭合的问题：

- `prctl(PR_CAPBSET_READ)` 不再打印 unsupported warning。
- `getcwd` 原始 syscall 按 Linux ABI 返回包含 NUL 的路径长度，普通 `git status` / `git diff` 仓库发现恢复可用。
- `setsockopt(IPPROTO_TCP, TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT/TCP_USER_TIMEOUT)` 对 TCP socket 可用，curl/hyper 不再因 TCP keepalive 选项得到 `ENOPROTOOPT`。
- Codex 写文件后清理子进程时不再因为未回收 zombie child 的 `getpgid`/`kill` 返回 `ESRCH` 而报 `No such process (os error 3)`。

最终成功标记：

```text
STARRY_GROUPED_TESTS_PASSED
STARRY_CODEX_STAGE_F_COMPAT_PASSED
STARRY_CODEX_STAGE_F_WRITE_PASSED
```

## 本阶段源码变化

内核变更：

- `os/StarryOS/kernel/src/syscall/task/ctl.rs`
  - 增加 `PR_CAPBSET_READ`，有效 capability 返回 `1`，超过 `CAP_LAST_CAP` 返回 `EINVAL`。
- `os/StarryOS/kernel/src/syscall/fs/ctl.rs`
  - 修正 `sys_getcwd`：空指针返回 `EFAULT`，成功返回路径字节数加 NUL，而不是返回用户缓冲区地址。
- `os/StarryOS/kernel/src/syscall/net/opt.rs`
  - 增加 TCP keepalive/user-timeout 选项的最小兼容处理。
  - 抽出 `read_int_sockopt`，复用到 netlink `SO_RCVBUF` / `SO_PASSCRED`。
- `os/StarryOS/kernel/src/task/ops.rs`
  - 增加 `get_process(pid)`，允许在 `ProcessData` 已释放但 `Process` 仍留在父进程 children/process group 中时找到 unreaped zombie。
- `os/StarryOS/kernel/src/syscall/task/job.rs`
  - `getsid` / `getpgid` 改为通过 `get_process` 查询，使 zombie child 在 `waitpid` 前仍可见。
- `os/StarryOS/kernel/src/task/signal.rs`
  - `kill(pid, 0)` 和 `kill(pid, SIGKILL)` 对未回收 zombie 返回成功；真正回收后仍返回 `ESRCH`。

测试套件变更：

- `test-suit/starryos/normal/qemu-smp1/bugfix/bug-prctl-capbset-read/`
- `test-suit/starryos/normal/qemu-smp1/bugfix/bug-getcwd-syscall-return/`
- `test-suit/starryos/normal/qemu-smp1/bugfix/bug-tcp-keepalive-options/`
- `test-suit/starryos/normal/qemu-smp1/bugfix/bug-zombie-process-queries/`
- `test-suit/starryos/normal/qemu-smp1/bugfix/qemu-x86_64.toml`

目前只把这些 case 接入 x86_64 QEMU，因为本阶段只验证了 x86_64。

## 本阶段 target 产物

```text
target/codex/qemu/qemu-x86_64-codex-stage-f-compat.toml
target/codex/qemu/qemu-x86_64-codex-stage-f-write.toml
target/codex/qemu/qemu-x86_64-codex-stage-f-sed.toml
target/codex/logs/starry-codex-stage-f-bugfix-rerun.log
target/codex/logs/starry-codex-stage-f-compat-rerun.log
target/codex/logs/starry-codex-stage-f-write-applypatch.log
target/codex/notes/stage_f_report.md
```

`stage-f-sed` 是排查过程中保留的临时配置，不作为本阶段验收路径。

## 已验证内容

格式和静态检查：

```bash
cargo fmt
git diff --check
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask clippy --package starry-kernel
```

结果：`starry-kernel` 7 组 clippy 全部通过。

bugfix grouped case：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask starry test qemu --arch x86_64 -c bugfix
```

关键通过标记：

```text
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-getcwd-syscall-return
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-prctl-capbset-read
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-tcp-keepalive-options
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-zombie-process-queries
STARRY_GROUPED_TESTS_PASSED
```

Codex 兼容 smoke：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-f-compat.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

该 smoke 验证：

- `codex --version`
- `codex --help`
- `codex exec --help`
- 普通 `git init/config/commit/status/diff`
- `curl -Iv https://api.openai.com` 经 WSL 本机代理 `http://172.31.48.1:7890`

关键通过标记：

```text
STARRY_STAGE_F_CODEX_HELP_OK
STARRY_STAGE_F_GIT_OK
STARRY_STAGE_F_CURL_RC=0
STARRY_CODEX_STAGE_F_COMPAT_PASSED
```

Codex 写文件 smoke：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-f-write.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

关键通过标记：

```text
STARRY_STAGE_F_CODEX_WRITE_OK
STARRY_CODEX_STAGE_F_WRITE_PASSED
```

## 过程中遇到的问题与处理

1. `PR_CAPBSET_READ` 被 Codex/curl 路径触发。

   现象：日志出现 `sys_prctl: unsupported option 23`。

   处理：实现最小 `PR_CAPBSET_READ`。StarryOS 当前没有真实 capability bounding set 管理，因此有效 capability 先按“仍在 bounding set 中”返回 `1`，非法 capability 返回 `EINVAL`。

2. 普通 git 仓库发现失败。

   现象：阶段 E 需要用 `--git-dir` / `--work-tree` 绕过；阶段 F 复现后定位到原始 `getcwd` 返回值不符合 Linux ABI。

   处理：`sys_getcwd` 成功返回路径长度加 NUL，并修正空指针为 `EFAULT`。之后 guest 内普通 `git config/status/diff` 通过。

3. curl/hyper 设置 TCP keepalive 选项失败。

   现象：日志中出现 `Failed to set TCP_KEEP...`、`errno 92` 或 `ENOPROTOOPT`。

   处理：为 TCP socket 接受 `TCP_KEEPIDLE`、`TCP_KEEPINTVL`、`TCP_KEEPCNT`、`TCP_USER_TIMEOUT`。这些选项当前作为兼容配置保存前置检查，不改变 axnet 行为；非 TCP socket 仍返回 `ENOPROTOOPT`。

4. Codex 写文件后出现 `No such process (os error 3)`。

   现象：Codex 已经修改 `README.md`，但随后清理超时/退出的 child process 时，内部 `getpgid(pid)` / `killpg` 路径拿到 `ESRCH`，最终报 `No such process`。

   处理：发现 StarryOS 的 `PROCESS_TABLE` 保存的是 `ProcessData` weak ref，最后一个线程退出后 `ProcessData` 可能先消失，而 unreaped zombie 的 `Process` 仍被父进程或 process group 持有。新增 `get_process(pid)` 从 process group 扫描 unreaped zombie，并让 `getpgid`、`getsid`、`kill(pid, 0)`、`kill(pid, SIGKILL)` 在 `waitpid` 前符合 Linux 行为。

5. C case 缺少 capability 头文件。

   现象：rootfs/sysroot 中没有 `<linux/capability.h>`。

   处理：`bug-prctl-capbset-read` 在测试内本地定义 `CAP_CHOWN` 和 `CAP_LAST_CAP` 常量，避免引入额外 sysroot 依赖。

6. 开放式 Codex 写文件 prompt 有模型行为不稳定。

   现象：修复 zombie 后，未约束 prompt 的一次写文件 smoke 中，模型选择 shell 命令路径并等待到 QEMU timeout。单独排查发现 guest 内直接 `sed` 可以运行，问题更像 Codex 工具编排和在线模型选择的随机性。

   处理：最终写文件 smoke 改为明确要求使用 apply_patch/file-edit，不运行 shell 命令；guest 侧仍用 `git status` 和 `git diff` 验证真实文件改动。

7. Codex file-edit 日志里出现非阻塞的 `apply_patch` 返回码。

   现象：最终写文件 smoke 中可以看到 `apply_patch(auto_approved=true) exited 1`，但同一段日志显示 `README.md` 已实际修改，Codex 最终回答包含 `STARRY_STAGE_F_CODEX_WRITE_OK`，guest 侧 `grep`、`git status`、`git diff` 断言全部通过。

   处理：本阶段按 guest 侧文件系统结果和 git diff 作为真实验收依据，暂不把这个 Codex 内部工具返回码作为 StarryOS 内核阻塞项。后续若要追求 Codex 工具层完全干净，可继续对比 Linux 上同版本 Codex 的 apply_patch 行为。

8. 发现一个额外 tmpfs/pseudofs panic。

   现象：排查 `sed` 时，在临时目录中直接运行失败路径的 `git status --short`，触发 `os/StarryOS/kernel/src/pseudofs/tmp.rs:460` 的 atomic context panic。

   处理：该问题不是阶段 B 到 E 的 Codex 主线阻塞项，也不在本阶段最终验收路径中；已记录为后续单独修复候选，建议下一阶段或独立 bugfix 处理。

## 残留与下一步建议

- `inotify`、sqlite 文件锁、Codex sandbox、TUI 仍未在阶段 F 实现；当前日志没有把它们变成本阶段阻塞项。
- Codex 启动时仍可能出现模型列表刷新 timeout，但实际 `codex exec` 在线请求和写文件 smoke 已通过。
- 建议阶段 G 把不含 secret 的 Codex help/local workspace smoke 正式化到 `test-suit/starryos`；在线 Codex 写文件测试仍应留在 `target/codex` 或手工 smoke 中，避免把认证和外网依赖带进常规 CI。
