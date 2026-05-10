# Codex CLI on StarryOS

This note records the current public demo flow for running OpenAI Codex CLI in a StarryOS x86_64 QEMU guest.

## Current Status

The supported demo path is `codex exec`, not the interactive TUI and not Codex's default Linux sandbox.

Verified locally:

- StarryOS boots the Codex Linux musl binary in x86_64 QEMU.
- `codex --version`, `codex --help`, `codex exec --help`, and `codex login status` run inside the guest.
- `rg` and ordinary `git init` / `git status` / `git diff` work in a guest workspace.
- A formal offline StarryOS test case injects prebuilt `codex` and `rg` assets through `assets.toml`.
- With a local authenticated rootfs and a reachable host proxy, online `codex exec` can read a TGOSKits source subset and make a file edit that is verified by guest-side `grep`, `git status`, and `git diff`.

Not claimed yet:

- Codex TUI.
- Codex default Linux sandbox semantics.
- Full TGOSKits compilation inside the guest.
- CI coverage for online Codex tasks.
- aarch64/riscv64 Codex validation.
- large, open-ended repository refactors.

## Offline Smoke Test

This is the safest public demo because it does not need an API key or network access.

From the repository root:

```bash
scripts/test/prepare_codex_assets.sh

PATH="$PWD/target/codex/qemu-build-x86_64-user:$PWD/target/codex/qemu-build-x86_64:$PATH" \
PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
  cargo xtask starry test qemu --arch x86_64 -c codex-help
```

The test case is:

```text
test-suit/starryos/normal/qemu-smp1/codex-help/qemu-x86_64.toml
test-suit/starryos/normal/qemu-smp1/codex-help/assets.toml
```

Expected success markers:

```text
STARRY_CODEX_STAGE_G_HELP_OK
STARRY_CODEX_STAGE_G_LOGIN_STATUS_OK
STARRY_CODEX_STAGE_G_LOCAL_WORKSPACE_OK
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

This proves the guest can start the fixed Codex CLI `0.115.0` asset, run basic Codex commands, use `rg`, and exercise a small git workspace.

## Local Online Demo

The online demo is intentionally local-only. It depends on an authenticated rootfs and a host proxy reachable from the QEMU guest. Do not commit or display any authentication files.

The `target/codex/...` paths in this section are prepared local artifacts and remain ignored by git.

From the repository root:

```bash
PATH="$PWD/target/codex/qemu-build-x86_64-user:$PWD/target/codex/qemu-build-x86_64:$PATH" \
PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

The local QEMU config:

- boots x86_64 StarryOS with 2 GiB memory;
- uses QEMU user networking;
- sets `CODEX_HOME=/root/.codex`;
- sets CA and proxy environment variables;
- extracts a TGOSKits source subset into `/root/tgoskits`;
- normalizes ownership with `chown -R root:root /root/tgoskits` so plain git discovery works;
- initializes a git baseline;
- asks Codex to complete one read-only source-understanding task;
- asks Codex to complete one constrained file-edit task;
- verifies the result with guest-side `grep`, `git status --short`, and `git diff`.

The online demo prints clear sections in the serial log:

```text
===== STAGE H READ TASK: PROMPT =====
===== STAGE H READ TASK: CODEX FINAL ANSWER =====
===== STAGE H WRITE TASK: CREATED FILE =====
===== STAGE H WRITE TASK: GIT STATUS =====
===== STAGE H WRITE TASK: GIT DIFF =====
===== STAGE H SUMMARY =====
```

Expected success markers:

```text
STARRY_STAGE_H_READ_TASK_OK
STARRY_STAGE_H_WRITE_TASK_OK
STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED
```

This proves Codex is not only printing help text: it is calling the model from inside the StarryOS guest, reading real TGOSKits paths, writing a file, and letting local guest tools verify the edit.

## Extended Local Online Demo

For a stronger local demonstration, the extended Stage H task reads a wider TGOSKits subset and writes an existing scratch review file.

Like the basic online demo, it assumes the local `target/codex/...` artifacts already exist.

From the repository root:

```bash
PATH="$PWD/target/codex/qemu-build-x86_64-user:$PWD/target/codex/qemu-build-x86_64:$PATH" \
PKG_CONFIG_PATH="$PWD/target/local-pkgconfig:$PWD/target/host-libs/pkgconfig" \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-h-extended-task.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

This task asks Codex to:

- run a single context script that prints excerpts from `scripts/axbuild/src/starry/test.rs`, `scripts/axbuild/src/test/qemu.rs`, `scripts/axbuild/src/test/case.rs`, `test-suit/starryos/GUIDE.md`, and the `codex-help` TOML files;
- explain the end-to-end `codex-help` flow from QEMU case discovery to `prebuilt-assets` injection and guest-side `codex --version` validation;
- edit only `STARRY_STAGE_H_EXTENDED_REVIEW.md`;
- prove the edit with guest-side `git status --short` and `git diff`.

Expected success markers:

```text
STARRY_STAGE_H_EXTENDED_READ_TASK_OK
STARRY_STAGE_H_EXTENDED_WRITE_TASK_OK
STARRY_CODEX_STAGE_H_EXTENDED_TASK_PASSED
```

During local runs, Codex may print transient model-list refresh, websocket retry, or `apply_patch(auto_approved=true) exited 1` messages. Treat the final guest-side markers, created file content, `git status`, and `git diff` sections as the authoritative result.

## Demo Script

A short presentation can follow this order:

1. Boot the offline `codex-help` case and point out the success markers.
2. Explain that `assets.toml` injects prebuilt Codex and ripgrep binaries without committing large artifacts.
3. Boot the local online Stage H config.
4. Show the read task prompt and Codex final answer.
5. Show the write task generated file.
6. Show `git status` proving only the target file changed.
7. Show `git diff` proving the content was written by the Codex task.
8. Optionally boot the extended Stage H config and show that it reads multiple TGOSKits paths before editing a scratch review file.

## Known Limits

The online flow is a local experiment, not a default CI test. It needs local authentication and a network path to the model provider.

The current validation target is x86_64 QEMU. Broader architecture support, TUI, default sandbox semantics, and full in-guest TGOSKits builds should be treated as later milestones.
