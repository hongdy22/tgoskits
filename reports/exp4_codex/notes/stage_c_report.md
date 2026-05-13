# 阶段 C 报告：auth.json、TLS、HTTPS 和最小在线 Codex

日期：2026-05-09
分支：`exp4_codex`
基线提交：`504fd3755`

## 结论

阶段 C 已完成。StarryOS guest 内已经确认 DNS、CA、TLS、HTTPS、ChatGPT 登录缓存识别，以及一次真实 Codex 在线请求。最终成功标记：

```text
STARRY_CODEX_ONLINE_OK
STARRY_CODEX_STAGE_C_ONLINE_PASSED
```

## 本阶段源码变化

本阶段发现并修复了一个真实阻塞问题：`epoll_wait` 在阻塞期间持有用户态 `events` buffer 的直接内核引用。Codex 在线请求期间，另一个线程可能改变或取消映射该用户缓冲区，旧实现返回事件时直接写入该引用，导致内核 page fault。

修复内容：

```text
os/StarryOS/kernel/src/mm/access.rs
os/StarryOS/kernel/src/syscall/io_mpx/epoll.rs
```

行为变化：

- `epoll_wait` 仍会先校验用户输出缓冲区。
- 等待期间只写内核临时 `epoll_event` buffer。
- 返回前通过 checked user-copy 写回用户缓冲区。
- 如果用户缓冲区在等待期间被取消映射，返回 `EFAULT`，不再 panic。

新增 x86_64 回归：

```text
test-suit/starryos/normal/qemu-smp1/bugfix/bug-epoll-wait-user-buffer-race/
test-suit/starryos/normal/qemu-smp1/bugfix/qemu-x86_64.toml
```

回归覆盖：一个线程阻塞在 `epoll_wait`，主线程 `munmap` 掉其 events buffer 后用 `eventfd` 唤醒；预期 `epoll_wait` 返回 `EFAULT`。

## 过程中遇到的问题与处理

1. 宿主机启动命令第一次没有进入 QEMU。

   现象：使用 `env PATH=...:$PATH` 启动时，宿主机 `PATH` 中有带空格的 Windows 路径片段，导致 `env` 把路径拆成了参数并报 `No such file or directory`。

   处理：改成 shell 变量赋值形式：

   ```bash
   PATH="/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH" \
   PKG_CONFIG_PATH="..." \
   cargo xtask ...
   ```

2. guest 内初始没有 `curl`。

   现象：阶段 C 要先验证 HTTPS，但 Codex 专用 rootfs 里没有 `/usr/bin/curl`。

   处理：在 guest 内通过 `apk update && apk add curl ca-certificates` 安装。安装后 `curl -Iv https://api.openai.com` 能完成 DNS、TCP、TLS、证书验证和 HTTP/2 响应头获取。

3. 在线 Codex 第一次运行触发内核 page fault。

   现象：`codex exec` 已经启动并进入模型请求流程，但在 `mcp startup: no servers` 后内核 panic：

   ```text
   Unhandled #PF @ <Epoll::poll_events>, fault_vaddr=...
   ```

   定位：用 `addr2line` 将 RIP 反查到 `Epoll::poll_events`。旧实现里 `do_epoll_wait` 在阻塞前把用户态 events buffer 转成 `&mut [epoll_event]`，然后在等待结束时直接写入。Codex 是多线程程序，等待期间该用户地址可能被改变或取消映射，内核直接写就会触发 page fault。

   处理：`epoll_wait` 改为使用内核临时 `Vec<epoll_event>` 接收 ready events，返回前再通过 `vm_write_slice` 做 checked user-copy。若用户 buffer 在等待期间失效，现在返回 `EFAULT`，不再 panic。

4. 修复后需要补可复现回归。

   处理：新增 `bug-epoll-wait-user-buffer-race`：一个线程阻塞在 `epoll_wait`，主线程 `munmap` 掉 events buffer，再写 `eventfd` 唤醒；预期 waiter 返回 `-1/EFAULT`。该用例已加入 x86_64 `bugfix` grouped case 并通过。

5. 第一次 targeted clippy 因宿主机环境失败。

   现象：`cargo xtask clippy --package starry-kernel` 首次没有带 `PKG_CONFIG_PATH`，`libudev-sys` 找不到 `libudev.pc`。

   处理：按阶段 A/B 已记录的宿主机 shim 设置 `PKG_CONFIG_PATH` 后重跑，`starry-kernel` 7 组 clippy check 全部通过。

6. 在线 Codex 运行中存在非阻塞噪声。

   现象：仍能看到 `sys_prctl: unsupported option 23`、`failed to refresh available models: timeout waiting for child process to exit`，以及 `curl` 设置 `TCP_KEEPIDLE`/`TCP_KEEPINTVL`/`TCP_KEEPCNT` 返回 errno 92。

   处理：这些都没有阻塞主请求。阶段 C 先记录为非阻塞现象，不在本阶段扩展修复范围。

## 本阶段 target 产物

```text
target/codex/qemu/qemu-x86_64-codex-stage-c-online.toml
target/codex/logs/starry-codex-stage-c-online.log
target/codex/notes/stage_c_report.md
```

`target/codex/rootfs/rootfs-x86_64-codex.img` 在首次 C 阶段运行时安装了 `curl` 和 `ca-certificates`，用于 HTTPS 前置验证。

## 已验证内容

guest 内环境：

```sh
HOME=/root
USER=root
SHELL=/bin/sh
TERM=xterm-256color
PATH=/usr/local/bin:/usr/bin:/bin:/sbin
CODEX_HOME=/root/.codex
SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
```

DNS 和 CA：

```text
/etc/resolv.conf -> nameserver 10.0.2.3
/etc/ssl/certs/ca-certificates.crt exists
```

HTTPS：

```text
curl -Iv https://api.openai.com
DNS resolved api.openai.com
TLSv1.3 handshake completed
certificate verified via OpenSSL
HTTP/2 response headers received
```

认证：

```text
codex login status -c 'cli_auth_credentials_store="file"'
Logged in using ChatGPT
```

最小在线 Codex：

```sh
codex exec \
  --skip-git-repo-check \
  --dangerously-bypass-approvals-and-sandbox \
  --color never \
  -C /root \
  --output-last-message /tmp/codex-stage-c-last-message.txt \
  'Reply with exactly: STARRY_CODEX_ONLINE_OK'
```

结果：

```text
STARRY_CODEX_ONLINE_OK
```

## 验证命令

```bash
cargo fmt
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask clippy --package starry-kernel
```

结果：

```text
clippy summary: 1 package(s), 7 check(s), 1 package(s) passed, 0 package(s) failed
all clippy checks passed
```

阶段 C 在线 QEMU：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
cargo xtask starry qemu \
  --target x86_64-unknown-none \
  --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-c-online.toml \
  --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

结果：

```text
STARRY_CODEX_STAGE_C_ONLINE_PASSED
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
STARRY_GROUPED_TESTS_PASSED
starry normal qemu summary:
passed (1):
  bugfix
failed (0):
  <none>
```

## 已观察到但不阻塞阶段 C 的问题

- Codex 启动时仍有 `sys_prctl: unsupported option 23` warning，和阶段 B 一致，当前不阻塞在线请求。
- Codex 在线运行中出现过 `failed to refresh available models: timeout waiting for child process to exit`，但主请求仍然完成并返回成功字符串。
- `curl` 会尝试设置 `TCP_KEEPIDLE`、`TCP_KEEPINTVL`、`TCP_KEEPCNT`，StarryOS 当前返回 errno 92；这不影响 TLS 握手和 HTTP/2 响应头获取。
- 首次运行 `bugfix` grouped case 时，rootfs staging 阶段仍有 debugfs ownership warning，和阶段 A 记录一致，不影响测试结果。

## 进入阶段 D 的建议

可以进入阶段 D：让 Codex 在 guest 内操作小 workspace。建议继续使用：

```sh
codex exec \
  --skip-git-repo-check \
  --dangerously-bypass-approvals-and-sandbox \
  -C /root/workspace/hello \
  '<small workspace prompt>'
```

优先验证只读文件摘要、写 README、`rg` 搜索，再验证 Codex 执行简单 shell 命令。
