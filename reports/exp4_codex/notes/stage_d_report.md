# 阶段 D 报告：小 workspace 读写、rg 和 shell 闭环

日期：2026-05-09
分支：`exp4_codex`
基线提交：`9dfb413ab`

## 结论

阶段 D 已完成。StarryOS guest 内的 Codex 已经能在小 workspace 中完成：

- 读取 `README.md` 和 `AGENTS.md`；
- 修改 `README.md` 并由 guest 侧读回验证；
- 使用 `rg` 搜索文件内容；
- 执行指定 shell command 并总结输出。

最终成功标记：

```text
STARRY_CODEX_STAGE_D_WORKSPACE_PASSED
```

## 本阶段源码变化

本阶段在实际 workspace smoke 中发现并修复了一个新的内核问题：`poll(2)` 在阻塞期间持有用户态 `pollfd` 数组中 `revents` 字段的直接可变引用。Codex 等待模型响应时会使用多线程和 I/O multiplexing；如果等待期间用户态取消映射了该数组，旧实现被事件唤醒后会直接写旧用户地址并触发内核 page fault。

修复文件：

```text
os/StarryOS/kernel/src/syscall/io_mpx/poll.rs
```

行为变化：

- `poll`/`ppoll` 入口仍先校验用户 `pollfd` 数组；
- 等待期间改为使用内核 `Vec<pollfd>` 副本；
- `do_poll` 只记录内核副本中的 revents 下标，不再跨阻塞点保存用户态字段引用；
- 返回前通过 `vm_write_slice` checked user-copy 写回完整 `pollfd` 数组；
- 如果用户数组在等待期间失效，现在返回 `EFAULT`，不再 panic。

新增 x86_64 回归：

```text
test-suit/starryos/normal/qemu-smp1/bugfix/bug-poll-wait-user-buffer-race/
test-suit/starryos/normal/qemu-smp1/bugfix/qemu-x86_64.toml
```

回归覆盖：一个线程阻塞在 `poll`，主线程 `munmap` 掉 `pollfd` 数组后用 `eventfd` 唤醒；预期 waiter 返回 `-1/EFAULT`。

## 本阶段 target 产物

```text
target/codex/qemu/qemu-x86_64-codex-stage-d-workspace.toml
target/codex/logs/starry-codex-stage-d-workspace.log
target/codex/notes/stage_d_report.md
```

`qemu-x86_64-codex-stage-d-workspace.toml` 是本地实验配置，使用 `target/codex/rootfs/rootfs-x86_64-codex.img`，并在 guest 内创建 `/root/workspace/hello`。

## 阶段 D workspace

guest 内初始目录：

```text
/root/workspace/hello/
  AGENTS.md
  README.md
  hello.sh
  src/main.rs
```

本阶段仍使用 `--skip-git-repo-check`，没有强制安装或验证 guest 内 `git`。阶段计划允许先完成普通目录读写，`git status`/`git diff` 放到阶段 E 或后续 D+ 补测。

## 过程中遇到的问题与处理

1. 在线 workspace smoke 首次卡在模型请求阶段。

   现象：`codex exec` 已经进入会话并开始读文件，但长时间不返回。宿主 WSL 使用本地转发代理，guest 没有继承宿主 `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`。

   处理：确认宿主代理端口 `172.31.48.1:7890` 可连接后，在阶段 D QEMU 配置中显式导出大小写代理变量。重跑后 Codex 在线请求能正常返回。

2. 第二次阶段 D 运行暴露 `poll(2)` 用户缓冲区 page fault。

   现象：第一个只读 Codex 请求在 `mcp startup: no servers` 后触发：

   ```text
   Unhandled #PF @ 0xffff80000034d530, fault_vaddr=VA:0x9fea9d6, error_code=0x3 (WRITE)
   ```

   定位：`addr2line` 将 RIP 定位到 `starry_kernel::syscall::io_mpx::poll::do_poll` 经 `poll_io` 回调写 `revents` 的路径。

   处理：`poll`/`ppoll` 改为内核副本等待，返回前 checked user-copy 写回；新增 `bug-poll-wait-user-buffer-race` 回归。

3. Codex 内部 patch helper 有一次非阻塞异常。

   现象：让 Codex 自行追加 README 行时，内部 file update 已经写入文件，但工具层返回 `No such process (os error 3)`，模型误以为失败并重试，导致重复行。

   处理：阶段 D smoke 改为明确要求 Codex 执行指定 `sh -lc` 追加命令，并在 guest 侧检查目标行只出现一次。这个异常没有阻塞阶段 D，但建议后续单独调查 Codex 内部 apply-patch helper 与 StarryOS 子进程等待语义的兼容性。

4. 运行中仍有非阻塞 warning。

   现象：`sys_prctl: unsupported option 23` 仍会出现；`models_manager` 偶发 refresh timeout。

   处理：本阶段记录为非阻塞现象。四个 `codex exec` 任务均完成，最终 marker 匹配。

## 已验证内容

阶段 D QEMU：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
cargo xtask starry qemu \
  --target x86_64-unknown-none \
  --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-d-workspace.toml \
  --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

结果：

```text
STARRY_STAGE_D_READ_OK
STARRY_STAGE_D_WRITE_OK
STARRY_STAGE_D_RG_OK
STARRY_STAGE_D_SHELL_OK
STARRY_CODEX_STAGE_D_WORKSPACE_PASSED
```

x86_64 bugfix 回归：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
cargo xtask starry test qemu --arch x86_64 -c bugfix
```

结果：

```text
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-epoll-wait-user-buffer-race
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-poll-wait-user-buffer-race
STARRY_GROUPED_TESTS_PASSED
starry normal qemu summary:
passed (1):
  bugfix
failed (0):
  <none>
```

格式和 clippy：

```bash
cargo fmt
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask clippy --package starry-kernel
git diff --check
```

结果：

```text
clippy summary: 1 package(s), 7 check(s), 1 package(s) passed, 0 package(s) failed
all clippy checks passed
git diff --check: passed
```

## 进入阶段 E 的建议

可以进入阶段 E：把工作区扩大到 TGOSKits 源码子集或全仓。建议先保持阶段 D 已验证的代理环境和 `--skip-git-repo-check`，再逐步加入 guest 内 `git status`/`git diff`，避免把 Git 兼容性和大仓扫描问题一次性混在一起。
