# 阶段 E 报告：扩大到 TGOSKits 源码子集

日期：2026-05-09
分支：`exp4_codex`
基线提交：`8ee5aeb73`

## 结论

阶段 E 已完成。StarryOS guest 内的 Codex 已经能以 `/root/tgoskits` 为工作目录，在一个 TGOSKits 源码子集中完成：

- 启动 `codex exec`；
- 使用 `rg` 查找真实源码路径；
- 引用真实 TGOSKits 路径返回结果；
- 生成一个不需要编译的小文件；
- 通过 guest 内 `git status` 和 `git diff --cached` 验证改动内容。

最终成功标记：

```text
STARRY_CODEX_STAGE_E_TGOSKITS_PASSED
```

## 本阶段源码变化

本阶段没有修改 tracked 源码。所有新增产物都在 `target/codex/` 下，用于本地实验和记录。

因此本阶段没有运行 `cargo fmt` / `cargo xtask clippy`。收尾检查：

```text
git status -sb: ## exp4_codex...origin/exp4_codex
git diff --check: passed
```

## 本阶段 target 产物

```text
target/codex/workspaces/stage_e_tgoskits_subset/
target/codex/workspaces/stage_e_tgoskits_subset.tar.gz
target/codex/notes/inject_stage_e_subset.debugfs
target/codex/qemu/qemu-x86_64-codex-stage-e-tgoskits.toml
target/codex/logs/starry-codex-stage-e-tgoskits.log
target/codex/notes/stage_e_report.md
```

`stage_e_tgoskits_subset.tar.gz` 被注入到 Codex 专用 rootfs 的 `/root/tgoskits-stage-e-subset.tar.gz`，QEMU 启动后解压为 `/root/tgoskits`。

## 注入的源码子集

本阶段按阶段计划采用 E1 方案：rootfs overlay 注入 TGOSKits 子集，而不是 guest 内 clone 全仓。

最终注入内容：

```text
AGENTS.md
README.md
README_CN.md
.claude/skills/starry-test-suit/SKILL.md
os/StarryOS/kernel/src/syscall/
test-suit/starryos/GUIDE.md
scripts/axbuild/src/lib.rs
scripts/axbuild/src/starry/
scripts/axbuild/src/test/
```

子集大小约 `143K`，足够覆盖阶段 E 的只读理解、路径引用、`rg` 搜索和小文件写入验证，同时避免把完整仓库和完整构建依赖一次性塞进 rootfs。

## 已验证内容

阶段 E QEMU：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
cargo xtask starry qemu \
  --target x86_64-unknown-none \
  --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-e-tgoskits.toml \
  --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

QEMU 配置里继续沿用阶段 D 已验证的代理环境：

```text
HTTP_PROXY=http://172.31.48.1:7890
HTTPS_PROXY=http://172.31.48.1:7890
ALL_PROXY=http://172.31.48.1:7890
```

guest 内直接验证：

```text
STARRY_STAGE_E_SOURCE_TREE_READY
STARRY_STAGE_E_GIT_BASELINE_READY
STARRY_STAGE_E_READ_MAP_SCRIPT_OK
```

Codex 只读任务使用 `/root/tgoskits` 为工作目录，执行一个受控 `rg` 脚本，最终输出包含：

```text
STARRY_STAGE_E_READ_OK
os/StarryOS/kernel/src/syscall/mod.rs
os/StarryOS/kernel/src/syscall/io_mpx/poll.rs
scripts/axbuild/src/starry/test.rs
```

Codex 写入任务执行受控脚本创建 `STARRY_STAGE_E_NOTES.md`，guest 侧随后验证文件存在，并用 git 检查未跟踪状态和 staged diff：

```text
?? STARRY_STAGE_E_NOTES.md
diff --git a/STARRY_STAGE_E_NOTES.md b/STARRY_STAGE_E_NOTES.md
+- StarryOS syscall dispatch entry: os/StarryOS/kernel/src/syscall/mod.rs
+- Poll user-buffer safety path: os/StarryOS/kernel/src/syscall/io_mpx/poll.rs
+- Starry qemu test flow entry: scripts/axbuild/src/starry/test.rs
+- Shared qemu case helpers: scripts/axbuild/src/test/qemu.rs
+STARRY_STAGE_E_WRITE_NOTE
```

最终日志：

```text
STARRY_CODEX_STAGE_E_TGOSKITS_PASSED
=== SUCCESS PATTERN MATCHED: (?m)^STARRY_CODEX_STAGE_E_TGOSKITS_PASSED\s*$ ===
```

## 过程中遇到的问题与处理

1. guest 内普通 `git` 仓库发现失败。

   现象：`git init` 后执行 `git config user.name ...` 报错：

   ```text
   fatal: not in a git directory
   ```

   处理：阶段 E 改为显式传入 git 目录和工作树：

   ```sh
   git --git-dir=/root/tgoskits/.git --work-tree=/root/tgoskits ...
   ```

   这样 `config`、`add`、`commit`、`status`、`diff --cached` 均可完成。这个问题没有阻塞阶段 E，但说明 StarryOS 里 Git 的仓库发现路径还需要后续单独调查。

2. 初版源码子集缺少项目本地 skill 文件。

   现象：Codex 读取 `AGENTS.md` 后识别到 StarryOS test-suit 相关任务会触发 `.claude/skills/starry-test-suit/SKILL.md`，但初版子集没有这个文件，Codex 尝试读取后长时间没有稳定收束。

   处理：把 `.claude/skills/starry-test-suit/SKILL.md` 加入注入子集。

3. 初版源码子集缺少 `scripts/axbuild/src/test/` 和 `scripts/axbuild/src/lib.rs`。

   现象：Codex 从 `scripts/axbuild/src/starry/test.rs` 继续追到 shared qemu helper，例如 `scripts/axbuild/src/test/qemu.rs`、`case.rs` 等，但初版子集没有这些路径。

   处理：把 `scripts/axbuild/src/test/` 和 `scripts/axbuild/src/lib.rs` 加入注入子集，让只读理解路径闭合。

4. 开放式只读 prompt 会导致 guest 内探索时间过长。

   现象：让 Codex 自由解释 StarryOS syscall/test 结构时，它会连续执行多次 `sed`/`rg`，在 guest 内等待时间较长，偶尔看起来像卡住。

   处理：阶段 E 的最终验证改为受控脚本 `/tmp/stage-e-read-map.sh`，脚本内部只跑一次带 `-m 5` 的 `rg`，输出重定向到文件，stdout 只保留固定 OK 标记。这样可以验证 Codex 能运行 `rg` 和引用真实路径，同时降低模型探索带来的随机性。

5. 模型选择和在线延迟影响实验稳定性。

   现象：较大的模型配合开放式 prompt 时，guest 内会出现数分钟等待；日志里也可见 `models_manager` refresh timeout。

   处理：最终阶段 E 使用 `--model gpt-5.4-mini` 和固定 marker prompt。`models_manager` refresh timeout 不影响实际请求完成。

6. 仍有非阻塞 warning。

   运行中仍会看到：

   ```text
   sys_prctl: unsupported option 23
   ```

   以及 `rg`/Codex 子进程偶发的 jemalloc `MADV_DONTNEED does not work` 提示。这些没有阻塞 `rg`、Codex 在线请求、文件写入或 git diff 验证。

## 进入阶段 F 的建议

可以进入阶段 F。建议优先把阶段 E 暴露出的兼容性问题拆开排查：

- 调查 `git init` 后普通 `git config/status/diff` 为什么不能通过当前目录自动发现 `.git`；
- 调查 Codex 内部 patch helper 曾在阶段 D 出现的 `No such process (os error 3)`，以及阶段 E 开放式命令探索时的长等待；
- 保持当前已验证代理配置，继续用受控脚本和固定 marker 建立更大的 Codex 行为回归；
- 后续再逐步扩大到完整 TGOSKits 仓库或 guest 内 shallow clone，不建议马上在 guest 内跑完整 `cargo xtask`。
