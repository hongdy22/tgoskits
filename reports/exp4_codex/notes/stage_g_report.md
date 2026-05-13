# 阶段 G 报告：Codex smoke 正式接入 StarryOS test-suit

日期：2026-05-10
分支：`exp4_codex`
起点提交：`836505c6c`

## 结论

阶段 G 已完成。本阶段把不含 secret 的 Codex smoke 固化为正式 StarryOS QEMU case：

```bash
cargo xtask starry test qemu --arch x86_64 -c codex-help
```

最终成功标记：

```text
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

## 本阶段源码变化

- `scripts/axbuild/src/test/case.rs`
  - 新增 `assets.toml` 触发的 `prebuilt-assets` pipeline。
  - 支持校验 host 侧预编译资产 SHA-256，并注入到 guest 绝对路径。
  - 支持 `source` 本地路径；相对路径先按仓库根目录解析，再按 case 目录解析。
  - 支持 raw-file `url` fallback 下载到 case cache，并继续按 SHA-256 校验。
  - 增加 guest path 防穿越检查和 mode 设置。
  - 增加 pipeline 相关单元测试。
- `scripts/axbuild/src/starry/test.rs`
  - 修复 test-only 的重复 helper 定义；否则 `cargo test -p axbuild` 无法编译。
- `test-suit/starryos/normal/qemu-smp1/codex-help/`
  - 新增 `assets.toml`，声明本地 Codex/ripgrep 资产、guest 注入路径和 SHA-256。
  - 新增 `qemu-x86_64.toml`，正式验证 Codex CLI help、空登录状态、rg、git 和本地 workspace diff。
- `test-suit/starryos/GUIDE.md`
  - 记录 `prebuilt-assets` pipeline、`assets.toml` 语义和使用约束。

## 资产来源

本阶段不提交大型二进制。正式 case 使用本地缓存：

```text
target/codex/assets/codex
target/codex/assets/rg
```

记录的 SHA-256：

```text
codex  440269f35afeb90d38115af844629d98705fb7266fdcd5fe7c040a78ebc75b85
rg     ebeaf56f8a25e102e9419933423738b3a2a613a444fd749d695e15eba53f71f2
```

guest 内验证版本：

```text
codex-cli 0.115.0
ripgrep 15.1.0
```

## codex-help 覆盖内容

`codex-help` 不需要 API key，不读取 `target/auth.json`，也不联网。guest 内执行：

- `codex --version`
- `codex --help`
- `codex exec --help`
- `rg --version`
- `codex login status -c 'cli_auth_credentials_store="file"'`
  - 使用空 `CODEX_HOME=/root/codex-stage-g-home`
  - 期望输出 `Not logged in`，返回码非 0
- 本地 workspace smoke：
  - `git init`
  - `git config`
  - `git commit`
  - 修改 `README.md`
  - `git status --short`
  - `git diff`
  - `rg -n --with-filename`

关键标记：

```text
STARRY_CODEX_STAGE_G_HELP_OK
STARRY_CODEX_STAGE_G_LOGIN_STATUS_OK
STARRY_CODEX_STAGE_G_LOCAL_WORKSPACE_OK
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

## 已验证内容

格式和静态检查：

```bash
cargo fmt
git diff --check
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask clippy --package axbuild
```

结果：`axbuild` 4 组 clippy 全部通过。

单元测试：

```bash
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo test -p axbuild test::case
```

结果：`test::case` 6 个测试通过。

正式 StarryOS QEMU case：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
  cargo xtask starry test qemu --arch x86_64 -c codex-help
```

最终通过：

```text
ok: codex-help
passed (1):
  codex-help
failed (0):
  <none>
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

## 过程中遇到的问题与处理

1. `cargo test -p axbuild test::case` 第一次缺少 `libudev.pc`。

   现象：host 侧 `libudev-sys` build script 找不到 `libudev.pc`。

   处理：与 clippy 保持一致，带上：

   ```text
   PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig
   ```

2. axbuild test-only 代码里已有重复 helper。

   现象：带上 `PKG_CONFIG_PATH` 后，`cargo test -p axbuild` 暴露 `write_qemu_build_config_with_max_cpu_num` 重复定义，并引用不存在的 `StarryTestGroup`。

   处理：删除未使用的旧 helper，保留当前测试实际调用的 `&str` 版本。

3. 初版 `codex-help` case 中 `rg` 单文件输出不带文件名前缀。

   现象：脚本期望 `README.md:2:...`，但 `rg -n PATTERN README.md` 输出 `2:...`，导致 grep 失败。失败路径又触发了阶段 F 已记录过的 tmpfs/pseudofs atomic context panic。

   处理：改为 `rg -n --with-filename PATTERN README.md`，使输出稳定包含文件名；复跑后 case 通过。

4. 大型二进制不适合进入 git。

   处理：新增 `assets.toml` pipeline，只提交资产声明、目标路径和 SHA-256；实际二进制仍放在 `target/codex/assets/` 或由 `url` fallback 准备。

## 残留与下一步建议

- 当前正式 case 只接入 x86_64，因为 Codex/rg 资产和验证都只覆盖了 x86_64。
- `codex-help` 仍使用本地缓存资产；如果要在 CI 中跑，需要先给 CI 准备同 SHA-256 的 `target/codex/assets/codex` 和 `target/codex/assets/rg`，或把 `assets.toml` 切到 raw-file `url` 下载源。
- 在线 Codex 写文件、API key、ChatGPT auth、外网代理仍不进入正式 test-suit，继续保留在 `target/codex` 手工 smoke。
