# StarryOS Codex Self-Evolve Demo 简明步骤

目标：在 StarryOS guest 里运行 Codex CLI，让它在真实 TGOSKits 仓库中寻找一个小的 syscall/内核语义 bug，并给出修复或下一步验证计划。

## 1. Host 准备

```bash
cd /home/threetu33/os_biglabB_task2/tgoskits
git switch test/starryos-prebuilt-cli-smoke
```

如果更新过 `target/auth.json`，重新注入 rootfs：

```bash
examples/starry/codex-cli/prepare_codex_rootfs.sh \
  --output-rootfs tmp/axbuild/rootfs/rootfs-x86_64-codex-online.img \
  --auth-json target/auth.json \
  --proxy http://172.31.48.1:7890
```

设置 QEMU 路径：

```bash
export PATH="$PWD/target/codex/qemu-build-x86_64:$PWD/target/codex/qemu-build-x86_64-user:$PATH"
```

## 2. 一键运行 Syscall Bug Hunt Demo

```bash
PATH="$PWD/target/codex/qemu-build-x86_64:$PWD/target/codex/qemu-build-x86_64-user:$PATH" \
cargo xtask starry qemu \
  --arch x86_64 \
  --qemu-config target/codex/qemu/qemu-x86_64-codex-tgoskits-syscall-hunt.toml \
  --rootfs tmp/axbuild/rootfs/rootfs-x86_64-codex-online.img
```

成功标记：

```text
STARRY_TGOSKITS_SYSCALL_HUNT_PASSED
```

这个命令会自动完成：

```text
启动 StarryOS -> clone TGOSKits -> 把 syscall bug-hunt prompt 交给 Codex -> 输出候选 bug/修复/验证计划 -> 打印 git status/diff
```

## 3. 手动 Demo：启动 QEMU

```bash
PATH="$PWD/target/codex/qemu-build-x86_64:$PWD/target/codex/qemu-build-x86_64-user:$PATH" \
cargo xtask starry qemu \
  --arch x86_64 \
  --rootfs tmp/axbuild/rootfs/rootfs-x86_64-codex-online.img
```

进入 `root@starry` 后执行：

```sh
export HOME=/root
export USER=root
export SHELL=/bin/sh
export TERM=xterm-256color
export PATH=/usr/local/bin:/usr/bin:/bin:/sbin
export CODEX_HOME=/root/.codex
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
. "$CODEX_HOME/starry-online-env"
```

检查工具：

```sh
command -v git
command -v rg
command -v codex
codex --version
codex login status -c 'cli_auth_credentials_store="file"'
```

## 4. 准备 TGOSKits 仓库

如果 guest 里已经有 `/tmp/tgoskits-syscall-hunt/repo`，可以跳过 clone。

```sh
rm -rf /tmp/tgoskits-syscall-hunt
mkdir -p /tmp/tgoskits-syscall-hunt
cd /tmp/tgoskits-syscall-hunt
git clone --depth 1 --branch dev https://github.com/rcore-os/tgoskits.git repo
cd repo
```

如果已经有仓库，直接进入：

```sh
cd /tmp/tgoskits-syscall-hunt/repo
```

确认仓库可用：

```sh
git status --short
test -f AGENTS.md
test -d os/StarryOS/kernel
test -d test-suit/starryos
```

## 5. 写入 Prompt

```sh
cat > /tmp/tgoskits-syscall-hunt-prompt.txt <<'EOF'
You are running inside a StarryOS x86_64 QEMU guest. The working directory is a real clone of the TGOSKits repository.

Task: find and, if practical, fix one small and well-scoped syscall or kernel semantic bug.

Rules:
- Treat Linux behavior as the reference.
- Prefer one small target from filesystem/directory semantics, process wait/exit semantics, or memory mapping semantics.
- Handle only one clear issue in this run. Do not do a broad refactor.
- First read `AGENTS.md`, `test-suit/starryos/GUIDE.md`, and the relevant StarryOS kernel/test-suit code.
- First explain the minimal Linux-vs-StarryOS differential idea, then decide whether to write a test.
- If it is safe to edit, prefer adding or updating one minimal regression test and one minimal fix.
- If time, tools, or environment limits prevent a safe fix, do not force a patch. Instead report the candidate bug, evidence, relevant files, and the next minimal validation command.
- Do not read, print, or copy any auth.json.

Use necessary read-only commands such as `rg`, `sed`, and `git status`. If you edit files, only edit files directly related to the selected issue.

Your final answer must be in Simplified Chinese and must include:
- `STARRY_TGOSKITS_SYSCALL_HUNT_DONE`
- selected syscall/semantic target
- bug hypothesis or confirmed issue
- key files inspected
- if files changed, changed files and validation commands; if no files changed, the next minimal validation plan
EOF
```

## 6. 交给 Codex

```sh
codex exec \
  --dangerously-bypass-approvals-and-sandbox \
  -C /tmp/tgoskits-syscall-hunt/repo \
  --output-last-message /tmp/codex-tgoskits-syscall-hunt.txt \
  "$(cat /tmp/tgoskits-syscall-hunt-prompt.txt)"
```

或者直接输入：

```sh
codex exec \
  --dangerously-bypass-approvals-and-sandbox \
  -C /tmp/tgoskits-syscall-hunt/repo \
  'Please read the code in the current directory and briefly introduce this repository. Answer in Simplified Chinese.'
```

或者更简单的任务：

```sh
codex exec \
  --dangerously-bypass-approvals-and-sandbox \
  -C /tmp/tgoskits-syscall-hunt/repo \
  'Hello, could you introduce youself in chinese?'
```

## 7. 查看结果

```sh
cat /tmp/codex-tgoskits-syscall-hunt.txt
fold -w 80 -s /tmp/codex-tgoskits-syscall-hunt.txt
grep -F 'STARRY_TGOSKITS_SYSCALL_HUNT_DONE' /tmp/codex-tgoskits-syscall-hunt.txt
git status --short
git diff --stat
git diff
```

## 8. 退出 QEMU

```text
exit
```
