# 阶段 H 报告：Codex 在 TGOSKits 子任务上试跑

日期：2026-05-10
分支：`exp4_codex`
起点提交：`f66f0874f`

## 结论

阶段 H 已完成。guest 内 Codex 已经能在注入的 TGOSKits 源码子集上完成一个只读理解任务和一个受控写文件任务，并由 guest 侧 shell、git status、git diff 做结果验收。

最终成功标记：

```text
STARRY_STAGE_H_READ_TASK_OK
STARRY_STAGE_H_WRITE_TASK_OK
STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED
```

本阶段没有修改已跟踪源码。新增内容都位于 `target/codex/`，用于本地实验复现。

## 本阶段 target 产物

```text
target/codex/workspaces/stage_h_tgoskits_subset/
target/codex/workspaces/stage_h_tgoskits_subset.tar.gz
target/codex/notes/inject_stage_h_subset.debugfs
target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml
target/codex/logs/starry-codex-stage-h-tgoskits-task-rerun3.log
target/codex/notes/stage_h_report.md
```

源码子集包含：

```text
AGENTS.md
README.md
README_CN.md
.claude/skills/starry-test-suit/SKILL.md
scripts/axbuild/src/lib.rs
scripts/axbuild/src/starry/
scripts/axbuild/src/test/
test-suit/starryos/GUIDE.md
test-suit/starryos/normal/qemu-smp1/build-x86_64-unknown-none.toml
test-suit/starryos/normal/qemu-smp1/codex-help/
```

## 已验证内容

运行命令：

```bash
PATH=target/codex/qemu-build-x86_64-user:target/codex/qemu-build-x86_64:$PATH \
  PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

QEMU 配置做了以下准备：

- 设置 `HOME`、`CODEX_HOME`、`SSL_CERT_FILE` 和 WSL 本机转发代理 `http://172.31.48.1:7890`。
- 将 `/root/tgoskits-stage-h-subset.tar.gz` 解压到 `/root/tgoskits`。
- 对 `/root/tgoskits` 执行 `chown -R root:root`，避免 host 侧 tar owner 触发 Git dubious ownership 保护。
- 检查 Starry test-suit 相关源码、文档和 `codex-help` case 文件存在。
- 在 `/root/tgoskits` 初始化 git baseline，并使用普通 `git config` / `git status` / `git diff` 操作工作区。
- 先在 Codex 之外运行 `/tmp/stage-h-read-context.sh`，确认上下文脚本能稳定输出需要的路径和符号。

只读任务：

```text
Run exactly sh /tmp/stage-h-read-context.sh and do not run any other command.
Based on that output, summarize how StarryOS QEMU test cases are discovered.
```

Codex 最终回答包含：

```text
STARRY_STAGE_H_READ_OK
scripts/axbuild/src/starry/test.rs
scripts/axbuild/src/test/qemu.rs
scripts/axbuild/src/test/case.rs
test-suit/starryos/GUIDE.md
test_commands
```

写文件任务：

```text
Use the apply_patch/file-edit tool directly and do not run shell commands.
Create a new file named STARRY_STAGE_H_CODEX_SMOKE.md.
```

guest 侧验证：

- `/root/tgoskits/STARRY_STAGE_H_CODEX_SMOKE.md` 被创建。
- 文件包含 `STARRY_STAGE_H_DOC_NOTE`。
- 文件包含 `test-suit/starryos/normal/qemu-smp1/codex-help/qemu-x86_64.toml`。
- 文件包含 `test-suit/starryos/normal/qemu-smp1/codex-help/assets.toml`。
- 文件包含 `scripts/axbuild/src/test/case.rs`。
- 文件包含短语 `no API key or network access`。
- `git status --short` 只显示 `?? STARRY_STAGE_H_CODEX_SMOKE.md`。
- `git diff --cached -- STARRY_STAGE_H_CODEX_SMOKE.md` 能看到上述内容。

最终日志：

```text
target/codex/logs/starry-codex-stage-h-tgoskits-task-rerun3.log
```

git ownership 修复的离线验证日志：

```text
target/codex/logs/starry-git-discovery-fixed.log
```

日志末尾匹配：

```text
STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED
=== SUCCESS PATTERN MATCHED: (?m)^STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED\s*$ ===
```

## 过程中遇到的问题与处理

1. 源码子集解压位置错误。

   现象：第一次运行把 tar 内容直接解到 `/root`，随后 `cd /root/tgoskits` 失败。

   处理：QEMU 配置改为先 `mkdir -p /root/tgoskits`，再 `tar -xzf ... -C /root/tgoskits`。

2. 普通 git config 在 `/root/tgoskits` 初版失败。

   现象：`git config user.name ...` 报 `fatal: not in a git directory`；后续单独 debug 发现 `git rev-parse --git-dir` 的真实错误是 `detected dubious ownership in repository at '/root/tgoskits'`。

   根因：`stage_h_tgoskits_subset.tar.gz` 在 host 侧生成，解压到 guest 时保留了 host 用户 uid/gid。guest 内以 root 运行 git 时，工作区 owner 与当前用户不一致，触发 Git safe.directory/dubious ownership 保护。

   处理：Stage H 配置在 tar 解压后执行 `chown -R root:root /root/tgoskits`，并改回普通 `git config`、`git add`、`git commit`、`git status --short` 和 `git diff --cached`。离线 git discovery smoke 已复现旧问题并验证 chown 后 plain git 通过：

   ```text
   STARRY_GIT_DISCOVERY_DUBIOUS_OWNER_REPRODUCED
   STARRY_GIT_DISCOVERY_CHOWN_FIXED
   ```

3. 开放式只读 prompt 容易跑成超长探索。

   现象：一次开放式读仓任务中，Codex 连续运行多条 `sed` / `rg`，超过预期时间。

   处理：改成先准备固定 `/tmp/stage-h-read-context.sh`，并要求 Codex “只运行这一条命令”。这样既保留真实源码上下文，也避免大仓探索不稳定。

4. Codex file-edit 日志里仍出现非阻塞的 apply_patch 返回码。

   现象：日志显示 `apply_patch(auto_approved=true) exited 1`，但同一段日志显示文件已创建，Codex 最终回答包含 `STARRY_STAGE_H_WRITE_OK`。

   处理：继续以 guest 侧真实文件、grep、git status 和 git diff 作为验收依据。最终验收全部通过，因此这不是阶段 H 的阻塞项。

5. Codex 启动时出现模型列表刷新 timeout。

   现象：日志中有 `failed to refresh available models: timeout waiting for child process to exit`。

   处理：显式指定 `--model gpt-5.4-mini`，实际 read/write 请求均完成。该 timeout 不阻断阶段 H 验收。

6. Stage H rerun 时 fail regex 误判 shell 提示。

   现象：一次复跑在 `rm -rf /root/tgoskits` 后出现 `/bin/sh: can't set tty process group: No such process`，旧配置里的裸 `No such process` fail regex 直接判失败。此时还没有进入 Codex read/write 任务。

   处理：将 Stage H TOML 的 fail regex 从裸 `No such process` 收窄为 `No such process (os error 3)`，只捕获阶段 F 曾出现的 Codex 子进程清理错误形态，不再误判 shell 的 TTY process-group 提示。复跑后 read task、write task、guest git diff 验收和最终成功标记全部通过。

## 残留与下一步建议

- 本阶段验证的是 x86_64 QEMU、Codex 0.115.0、外网经 WSL 本机代理的路径。
- 本阶段没有把在线 Codex 任务纳入正式 `test-suit/starryos`，因为它依赖认证、网络和代理。
- `/root/tgoskits` 下普通 git discovery 的 Stage H 残留已定位为 tar owner 问题，并通过解压后 `chown -R root:root` 解决；当前 Stage H 配置不再使用显式 `--git-dir` / `--work-tree` 绕过。
- 阶段 F/G 中记录的 tmpfs/pseudofs failure-path panic 已在后续清理中修复，见下方补充记录。

## 后续清理记录（2026-05-10）

阶段 H 之后补了四项工程化清理：

1. 保留复杂 prompt，但让演示输出更清楚。

   `target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml` 已增加分段输出，会在串口日志中明确打印：

   ```text
   ===== STAGE H READ TASK: PROMPT =====
   ===== STAGE H READ TASK: CODEX FINAL ANSWER =====
   ===== STAGE H WRITE TASK: CREATED FILE =====
   ===== STAGE H WRITE TASK: GIT DIFF =====
   ===== STAGE H SUMMARY =====
   ```

   这样现场展示时可以直接看到“给 Codex 的任务是什么、Codex 回答了什么、文件实际写成什么、git diff 如何证明只改了目标文件”。

2. 调查 `apply_patch(auto_approved=true) exited 1` 噪声。

   对照日志显示，Codex 写文件任务里虽然出现过 `apply_patch(auto_approved=true) exited 1`，但同一轮已经生成文件 delta，最终回答包含 `STARRY_STAGE_H_WRITE_OK`，并且 guest 侧 `grep`、`git status --short`、`git diff --cached` 均通过。

   本地 Codex 源码也显示 apply_patch runtime 会把错误返回码和已经提交的 delta 分开处理：失败时仍会通过 `failure.into_parts().1` 取出已提交 delta 并更新 diff tracker。因此当前结论是：这条属于 Codex tool/runtime 层的非阻塞状态噪声，不是 StarryOS 的功能阻塞项。在线演示继续以 guest 侧真实文件和 git diff 作为验收依据。

3. 修复 tmpfs/pseudofs failure-path panic。

   修复点在 `os/StarryOS/kernel/src/pseudofs/tmp.rs`：

   - `MemoryNode::drop` 不再清理目录 entries，避免任务清理/失败路径中获取阻塞 mutex。
   - inode slab 和 inode metadata 改为 `SpinNoIrq`，因为 `release_inode()` 仍可能在 Drop 中执行。
   - `unlink` / `rename` 在正常系统调用上下文中提前清理被移除目录的 entries，并避免在父目录 entries 锁内 drop 被移除项。

   新增回归用例：

   ```text
   test-suit/starryos/normal/qemu-smp1/bugfix/bug-tmpfs-cwd-drop-safe/
   ```

   用例会让子进程 `chdir` 到 tmpfs 目录，父进程在子进程仍持有 cwd 时 `rmdir`，然后释放子进程退出，覆盖“目录已 unlink、cwd 稍后 drop”的旧 failure path。

4. 验证结果。

   已运行：

   ```bash
   cargo fmt
   PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
     cargo xtask clippy --package starry-kernel
   PATH="$PWD/target/codex/qemu-build-x86_64-user:$PWD/target/codex/qemu-build-x86_64:$PATH" \
   PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
     cargo xtask starry test qemu --arch x86_64 -c bugfix
   ```

   结果：

   ```text
   clippy summary: 1 package(s), 7 check(s), 1 package(s) passed, 0 package(s) failed
   STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-tmpfs-cwd-drop-safe
   STARRY_GROUPED_TESTS_PASSED
   ok: bugfix
   ```
