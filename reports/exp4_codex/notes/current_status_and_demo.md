# Codex on StarryOS 当前状态与演示流程

日期：2026-05-10
分支：`exp4_codex`
当前提交：`f66f0874f`

## 一句话结论

Codex CLI 已经可以在 StarryOS x86_64 QEMU 环境中启动、读取配置、运行 help/login/status/git/rg 这类本地命令；在带认证和代理的本地实验 rootfs 中，也已经完成过一次在线 `codex exec`，并让 Codex 在 TGOSKits 源码子集里读代码、创建文件，再由 guest 内 `git diff` 验证结果。

当前重点是：**可以展示“Codex 已经在 StarryOS 上跑起来，并能完成小型 coding-agent 闭环”**；但还不是完整 Linux 桌面环境里的 Codex 全功能替代。

## 当前已经完成的能力

可以稳定展示：

- StarryOS x86_64 QEMU 启动 Codex musl 静态二进制。
- `codex --version`、`codex --help`、`codex exec --help`。
- 空 `CODEX_HOME` 下的 `codex login status`，确认不会误读 secret。
- guest 内 `rg --version` 和 `rg -n --with-filename` 搜索。
- guest 内小 workspace 的 `git init`、`git config`、`git commit`、`git status --short`、`git diff`。
- 正式 StarryOS test-suit case：`codex-help`，不需要 API key，不联网。
- 带本地认证和 WSL 代理时，`codex exec` 可以访问模型并返回结果。
- Codex 可以在注入的 TGOSKits 源码子集中阅读真实源码路径。
- Codex 可以创建一个受控 markdown 文件，guest 内用 `grep`、`git status`、`git diff --cached` 验证改动。
- Stage H 在线演示入口会直接打印 prompt、Codex 最终回答、生成文件内容和 `git diff` 证据，便于现场说明。
- 阶段 F/G 记录过的 tmpfs/pseudofs failure-path panic 已修复，并新增 x86_64 bugfix 回归覆盖。

已经落地到源码的关键工程改动：

- 修复了 Codex 路径触发的若干 StarryOS 兼容性问题，包括 `PR_CAPBSET_READ`、`getcwd` Linux ABI 返回值、TCP keepalive sockopt、zombie child 的 `getpgid`/`kill` 查询行为。
- 修复 tmpfs 目录节点在任务清理/失败路径中持有阻塞锁导致的 `might_sleep()` panic：`MemoryNode::drop` 不再清理目录项，inode slab/metadata 改用非睡眠锁，并在 unlink/rename 正常路径清理目录项。
- 新增 Starry test-suit 的 `prebuilt-assets` pipeline，用 `assets.toml` 声明 Codex/ripgrep 这类大二进制的 host 缓存路径、guest 注入路径和 SHA-256。
- 新增正式测试用例 `test-suit/starryos/normal/qemu-smp1/codex-help/`。
- 新增 bugfix 回归用例 `test-suit/starryos/normal/qemu-smp1/bugfix/bug-tmpfs-cwd-drop-safe/`。

## 还没有完成的能力

暂时不要对外宣称已经完成：

- Codex TUI 交互界面。
- Codex 默认 Linux sandbox、landlock/seccomp 或完整隔离语义；当前在线任务使用 bypass/danger-full-access 路径。
- 在 guest 内编译整个 TGOSKits 或运行完整 `cargo xtask`。
- 把在线 Codex/API key/ChatGPT auth 纳入正式 CI。
- aarch64/riscv64 上的 Codex 验证。
- 多个 Codex session 并发、MCP、浏览器登录流。
- 完整 inotify/file watcher 语义。
- 全仓大规模自动修改。
- Codex file-edit 日志中仍见过非阻塞的 `apply_patch(auto_approved=true) exited 1` 返回码噪声；目前 guest 侧文件、`grep`、`git status`、`git diff` 验收都能通过，暂不把它视为 StarryOS 阻塞项。

## 演示前准备

从仓库根目录开始：

```bash
cd /home/threetu33/os_biglabB_task2/tgoskits
git status -sb
```

确认本地实验资产存在：

```bash
ls -lh \
  target/codex/assets/codex \
  target/codex/assets/rg \
  target/codex/rootfs/rootfs-x86_64-codex.img
```

公开演示时建议先跑不含 secret 的 `codex-help`。如果要展示在线 Codex 读写 TGOSKits 子任务，需要确认：

- 当前 rootfs 已准备好 `/root/.codex/auth.json`。
- WSL/宿主机代理可从 guest 访问，当前配置使用 `http://172.31.48.1:7890`。
- 不要展示、提交或粘贴任何 `auth.json` 内容。

## 演示 1：正式 test-suit 的离线 smoke

这是最适合公开展示的路径：不需要 API key，不联网，不读 `target/auth.json`。

运行：

```bash
PATH="$PWD/target/codex/qemu-build-x86_64-user:$PWD/target/codex/qemu-build-x86_64:$PATH" \
PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
  cargo xtask starry test qemu --arch x86_64 -c codex-help
```

你可以向别人说明：

- 这个命令会启动 StarryOS x86_64 QEMU。
- 测试框架会通过 `assets.toml` 校验并注入 `codex` 和 `rg`。
- guest 内会运行 Codex help、login status、rg、git、本地 workspace diff。

看到这些标记就算成功：

```text
STARRY_CODEX_STAGE_G_HELP_OK
STARRY_CODEX_STAGE_G_LOGIN_STATUS_OK
STARRY_CODEX_STAGE_G_LOCAL_WORKSPACE_OK
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

最终通常会看到：

```text
ok: codex-help
passed (1):
  codex-help
failed (0):
  <none>
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

这一段适合证明：Codex 二进制本身、基础文件系统、进程、git、rg 和测试套件注入流程都已经在 StarryOS guest 里跑通。

## 演示 2：在线 Codex 读写 TGOSKits 子任务

这是更完整的 coding-agent 闭环演示，但依赖本地认证和代理。

运行：

```bash
PATH="$PWD/target/codex/qemu-build-x86_64-user:$PWD/target/codex/qemu-build-x86_64:$PATH" \
PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

这个 QEMU 配置会自动完成：

- 设置 `HOME`、`CODEX_HOME`、CA 和代理环境变量。
- 把 TGOSKits 源码子集解压到 guest 的 `/root/tgoskits`。
- 初始化 git baseline。
- 先验证 Starry test-suit 相关源码路径可被 `rg` 搜索到。
- 调用 `codex exec` 做只读理解任务。
- 再调用 `codex exec` 创建 `STARRY_STAGE_H_CODEX_SMOKE.md`。
- 用 guest 内 `grep`、`git status --short`、`git diff --cached` 验证文件内容和改动范围。
- 直接在串口日志中打印 read/write 两个任务的 prompt、Codex 最终回答、生成文件正文、git status 和 git diff。

更具体地说，`target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml` 里主要做了这些事：

1. 配置 QEMU 启动参数。

   - 使用 `q35` machine。
   - 使用 `-nographic`，所以输出直接在当前终端里显示。
   - 给 guest 分配 `2G` 内存。
   - 挂载 `target/codex/rootfs/rootfs-x86_64-codex.img` 作为 virtio-blk 磁盘。
   - 打开 QEMU user network，方便 guest 通过宿主机代理访问外网。
   - 使用 `-snapshot`，所以这次运行对磁盘镜像的写入不会持久保存。

2. 配置 guest 里的环境变量。

   - `HOME=/root`
   - `CODEX_HOME=/root/.codex`
   - `SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt`
   - `HTTP_PROXY` / `HTTPS_PROXY` / `ALL_PROXY` 指向 `http://172.31.48.1:7890`
   - `PATH=/usr/local/bin:/usr/bin:/bin:/sbin`

   这里的代理地址是当前 WSL/宿主机环境使用的本地转发端口。如果换机器演示，需要按实际代理地址调整。

3. 准备 `/root/tgoskits` 工作区。

   - 删除旧的 `/root/tgoskits`。
   - 创建新的 `/root/tgoskits`。
   - 解压 `/root/tgoskits-stage-h-subset.tar.gz`。
   - 执行 `chown -R root:root /root/tgoskits`，避免 tar 保留 host 侧 uid/gid 后触发 Git 的 dubious ownership 保护。
   - 检查 `AGENTS.md`、`scripts/axbuild/src/starry/test.rs`、`scripts/axbuild/src/test/qemu.rs`、`scripts/axbuild/src/test/case.rs`、`test-suit/starryos/GUIDE.md`、`codex-help` case 等关键文件存在。

4. 初始化 git baseline。

   - 在 guest 的 `/root/tgoskits` 里执行 `git init`。
   - 使用普通 `git config` 配置临时用户名和邮箱。
   - 把源码子集全部 commit 成初始版本。
   - 使用普通 `git status --short` 检查工作区是干净的。

   这里已经不再需要 `git --git-dir=/root/tgoskits/.git --work-tree=/root/tgoskits ...` 绕过。之前的失败根因不是 StarryOS 的 `getcwd`/stat 兼容问题，而是源码子集 tar 从 host 解包时保留了非 root owner，Git 出于安全保护报 `detected dubious ownership`。统一 `chown` 后，plain git discovery 可以正常工作。

5. 先做 Codex 之外的上下文检查。

   - 在 guest 里生成 `/tmp/stage-h-read-context.sh`。
   - 这个脚本用 `rg` 搜索 StarryOS test-suit 发现逻辑相关符号。
   - 先直接运行脚本，确认它能输出真实路径和 `test_commands`、`prebuilt-assets` 等关键字。

6. 运行 Codex 只读任务。

   - 调用 `codex exec`。
   - 指定 `--model gpt-5.4-mini`。
   - 工作目录是 `/root/tgoskits`。
   - 要求 Codex 只运行 `/tmp/stage-h-read-context.sh`，然后总结 StarryOS QEMU case 是怎么发现的。
- 最终回答必须包含 `STARRY_STAGE_H_READ_OK` 和几个真实源码路径。
- 之后用 `grep` 检查 Codex 输出，确认回答内容符合预期。
- 日志中会用 `===== STAGE H READ TASK: ... =====` 分段展示任务、执行、最终回答和验证。

7. 运行 Codex 写文件任务。

   - 再次调用 `codex exec`。
   - 要求 Codex 使用 file-edit/apply_patch 工具，创建 `STARRY_STAGE_H_CODEX_SMOKE.md`。
   - 明确要求不运行 shell 命令、不修改其他文件。
   - 文件必须包含 `STARRY_STAGE_H_DOC_NOTE`、`codex-help` 的两个配置路径、`scripts/axbuild/src/test/case.rs` 和短语 `no API key or network access`。
   - 之后用 `grep` 检查文件内容。
- 用 `git status --short` 确认只有这个新文件发生变化。
- 用 `git diff --cached` 确认 diff 里包含预期内容。
- 日志中会用 `===== STAGE H WRITE TASK: ... =====` 分段展示任务、Codex 最终回答、生成文件正文、git status 和 git diff。

8. 输出最终成功标记。

   最后打印：

   ```text
   STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED
   ```

   `cargo xtask starry qemu` 会根据 TOML 里的 `success_regex` 匹配这个标记；如果日志里出现 panic、page fault、segmentation fault、`No such process (os error 3)`、`sys_prctl: unsupported option 23` 等内容，则会按 `fail_regex` 判失败。这里没有匹配裸的 `No such process`，是为了避免把 shell 的 TTY process-group 提示误判成 Codex 任务失败。

看到这些标记就算成功：

```text
CODEX_STAGE_H_ENV_READY
STARRY_STAGE_H_SOURCE_TREE_READY
STARRY_STAGE_H_GIT_BASELINE_READY
STARRY_STAGE_H_READ_CONTEXT_OK
STARRY_STAGE_H_READ_TASK_OK
STARRY_STAGE_H_WRITE_TASK_OK
STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED
```

现场展示时，建议重点停留在这些分段：

```text
===== STAGE H READ TASK: CODEX FINAL ANSWER =====
===== STAGE H WRITE TASK: CREATED FILE =====
===== STAGE H WRITE TASK: GIT DIFF =====
===== STAGE H SUMMARY =====
```

如果日志里偶尔出现：

```text
/bin/sh: can't set tty process group: No such process
```

这条本身不是 Codex read/write 任务失败，而是 guest shell 的 TTY process-group 提示。当前 Stage H TOML 的 `fail_regex` 已收窄为匹配 `No such process (os error 3)`，不会再因为这条提示误判失败；是否成功仍以 `STARRY_STAGE_H_READ_TASK_OK`、`STARRY_STAGE_H_WRITE_TASK_OK` 和 `STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED` 为准。

这一段适合证明：Codex 不只是能显示 help，而是真的能在 StarryOS guest 里联网调用模型、阅读仓库上下文、生成文件，并让本地工具验证修改结果。

## 展示时推荐说法

可以这样介绍整体流程：

```text
我们不是先追求完整 TUI 或完整 Linux sandbox，而是把目标拆成几个可验证闭环。
第一步让 StarryOS 跑起 Codex 的 Linux musl 二进制。
第二步把 codex --help、login status、rg、git、本地 workspace diff 做成正式 QEMU 测试。
第三步在带本地认证和代理的实验 rootfs 中，让 Codex 在线完成一次 TGOSKits 子任务。
现在已经证明：Codex CLI 可以在 StarryOS 上完成小型 coding-agent 工作流。
后续再继续补 TUI、sandbox、更多架构和更大仓库工作流。
```

如果演示时间短，只跑“演示 1”。如果想展示真正的模型调用和文件修改，再跑“演示 2”。

## 重要文件索引

计划和阶段报告：

```text
target/codex_on_starryos_plan.md
target/codex/notes/stage_f_report.md
target/codex/notes/stage_g_report.md
target/codex/notes/stage_h_report.md
target/codex/notes/stage_i_report.md
```

正式测试：

```text
test-suit/starryos/normal/qemu-smp1/codex-help/qemu-x86_64.toml
test-suit/starryos/normal/qemu-smp1/codex-help/assets.toml
test-suit/starryos/normal/qemu-smp1/bugfix/bug-tmpfs-cwd-drop-safe/
```

本地实验：

```text
target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml
target/codex/logs/starry-codex-stage-h-tgoskits-task-rerun3.log
target/codex/workspaces/stage_h_tgoskits_subset.tar.gz
```

不要公开：

```text
target/auth.json
target/codex/overlay/root/.codex/auth.json
rootfs 中的 /root/.codex/auth.json
```
