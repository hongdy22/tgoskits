# 阶段 I 报告：演示整理与 failure-path 清理

日期：2026-05-10
分支：`exp4_codex`

## 结论

阶段 I 的第一轮清理已完成：

- 保留 Stage H 的复杂 Codex prompt，没有改成过短任务。
- 在线演示 TOML 会在串口日志中清楚展示 prompt、Codex 最终回答、生成文件内容、git status 和 git diff。
- `apply_patch(auto_approved=true) exited 1` 已调查，当前判断为 Codex tool/runtime 层的非阻塞状态噪声；guest 侧文件和 git diff 验收仍是准确信号。
- tmpfs/pseudofs failure-path panic 已修复，并补了 x86_64 bugfix 回归。

## 代码改动

tmpfs 修复：

```text
os/StarryOS/kernel/src/pseudofs/tmp.rs
```

核心处理：

- `MemoryNode::drop` 不再清理目录 entries，避免在任务清理/失败路径里获取阻塞 mutex。
- inode slab 和 inode metadata 改为 `SpinNoIrq`，因为 `release_inode()` 仍会在 Drop 中执行。
- `unlink` / `rename` 在正常系统调用上下文提前清理被移除目录 entries，并避免在父目录 entries 锁内 drop 被移除项。

回归用例：

```text
test-suit/starryos/normal/qemu-smp1/bugfix/bug-tmpfs-cwd-drop-safe/
test-suit/starryos/normal/qemu-smp1/bugfix/qemu-x86_64.toml
```

用例逻辑：

- 子进程 `chdir` 到 `/tmp/bug-tmpfs-cwd-drop-safe`。
- 父进程在子进程仍持有 cwd 时 `rmdir` 该目录。
- 父进程释放子进程退出。
- 重复 8 次，验证 “已 unlink 的 cwd 后续 drop” 不再 panic。

## 演示入口改进

本地在线演示仍使用：

```text
target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml
```

该 TOML 现在会打印这些分段：

```text
===== STAGE H READ TASK: PROMPT =====
===== STAGE H READ TASK: CODEX FINAL ANSWER =====
===== STAGE H WRITE TASK: CREATED FILE =====
===== STAGE H WRITE TASK: GIT STATUS =====
===== STAGE H WRITE TASK: GIT DIFF =====
===== STAGE H SUMMARY =====
```

演示时重点看：

- `STARRY_STAGE_H_READ_TASK_OK`
- `STARRY_STAGE_H_WRITE_TASK_OK`
- `STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED`
- 生成文件正文 `STARRY_STAGE_H_CODEX_SMOKE.md`
- `git diff --cached -- STARRY_STAGE_H_CODEX_SMOKE.md`

## apply_patch 噪声调查

观察到的现象：

```text
apply_patch(auto_approved=true) exited 1
```

但同一段 Stage H 日志里：

- 文件 delta 已出现。
- Codex 最终回答包含 `STARRY_STAGE_H_WRITE_OK`。
- guest 侧 `grep` 检查目标文件内容通过。
- `git status --short` 只显示新文件。
- `git diff --cached` 包含预期内容。

源码侧观察：

- `codex-rs/core/src/tools/runtimes/apply_patch.rs` 中，runtime 会用 `result.is_err()` 映射 exit code。
- 失败分支仍通过 `failure.into_parts().1` 取出已提交 delta，并追加到 `committed_delta`。
- 事件层会把非零 exit code 显示为失败状态，但这不等于 guest 文件系统中的最终内容失败。

结论：当前按非阻塞噪声处理；继续使用 guest 侧真实文件、`grep`、`git status` 和 `git diff` 作为最终验收。

## 验证

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
