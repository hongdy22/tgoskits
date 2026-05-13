# Exp2 StarryOS Syscall 兼容性报告

日期：2026-05-09

## 1. 实验目标

Exp2 的目标是在 Exp1 pipeline 的基础上，让 Codex 自动寻找 StarryOS 中与 Linux 不一致的 syscall/应用兼容性问题，并形成“小问题、小修复、小回归”的上游 PR。

当时 pipeline 的目标可以概括为：

1. Developer Codex 围绕 StarryOS syscall/应用兼容性自动选题。
2. 先写或复用最小 Linux 用户态测例，建立 Linux 行为基准。
3. 在 StarryOS riscv64 QEMU 中运行同一测例，形成 Linux/StarryOS 差分。
4. 根据差分定位根因，做最小修复。
5. 补长期 QEMU 回归测试。
6. Reviewer Codex 以只读方式复核证据链、测试覆盖和风险，用 `PASS` / `REVISE` / `REJECT` 控制闭环。
7. Reviewer `PASS` 后再拆成上游 PR。

首批选题范围主要集中在三类：

- 文件/目录语义；
- 进程等待/退出语义；
- 内存映射语义。

这和后续 exp3 BusyBox 不同：exp2 更偏 syscall 源码级语义差分，不以某个应用脚本通过为唯一目标。

## 2. Pipeline 在 Exp2 中的作用

Exp2 不是手工枚举所有 syscall 逐个排查，而是借助 Exp1 的 Codex-only pipeline 做闭环探索。

核心机制是：

```text
Syncer: 同步 tgoskits 到 upstream/dev
Developer: 选择一个 syscall 语义点 -> 写 Linux 基准 -> StarryOS 差分 -> 修复 -> 补回归
Reviewer: 复核 Linux 语义、错误码、边界条件、回归测试和补丁范围
Committer/人工整理: 把 PASS 结果拆成小 PR
```

这个流程对 syscall bug 特别有效，因为很多问题不是“缺少整个 syscall”，而是 Linux ABI 的细节不一致，例如：

- invalid flags 是否应先返回 `EINVAL`；
- legacy syscall 是否应该忽略新 syscall 的额外 flags 参数；
- 是否应该在路径解析前就拒绝非法参数；
- symlink 默认是否跟随；
- `errno` 优先级是否与 Linux 一致；
- 某个参数的边界值是否应返回错误而不是成功。

这些细节靠大应用测试很容易被埋掉，而 pipeline 强制 Developer 抽取最小 C regression，再由 Reviewer 复核 Linux 行为，因此比较适合沉淀成可 review 的小 PR。

## 3. PR 总览

Exp2 最终拆成 8 个已合入 PR：

| PR | 标题 | 主题 |
| --- | --- | --- |
| [#449](https://github.com/rcore-os/tgoskits/pull/449) | `fix(file): reject invalid linkat flags and preserve symlink semantics` | `linkat` flags 与 symlink 语义 |
| [#450](https://github.com/rcore-os/tgoskits/pull/450) | `fix(mm): accept fd 0 for file mmap and ignore fd for anonymous mappings` | `mmap` fd 0 与 anonymous fd 语义 |
| [#451](https://github.com/rcore-os/tgoskits/pull/451) | `fix(file): honor RENAME_NOREPLACE in renameat2` | `renameat2` flags 与 `RENAME_NOREPLACE` |
| [#460](https://github.com/rcore-os/tgoskits/pull/460) | `fix(file): validate faccessat2 mode and flags` | `faccessat2` mode/flags 校验 |
| [#461](https://github.com/rcore-os/tgoskits/pull/461) | `fix(process): reject invalid wait4 options` | `wait4` invalid options |
| [#462](https://github.com/rcore-os/tgoskits/pull/462) | `fix(file): validate fchmodat2 flags` | `fchmodat2` flags 与 legacy dispatch |
| [#463](https://github.com/rcore-os/tgoskits/pull/463) | `fix(file): reject zero-size readlinkat buffers` | `readlinkat(size=0)` |
| [#464](https://github.com/rcore-os/tgoskits/pull/464) | `fix(file): handle mknodat type-zero regular files` | `mknodat` mode type bits |

这些 PR 的共同特点是：

- 每个 PR 只处理一个明确 syscall 语义点；
- 每个 PR 都包含 StarryOS QEMU regression；
- 大多先在 Linux 上运行同一个 C 测例确认预期行为；
- 修复范围集中在 `os/StarryOS/kernel/src/syscall/...` 和对应 `test-suit/starryos/.../bugfix` 用例。

## 4. 各 PR 问题与分析

### 4.1 PR #449：`linkat` flags 与 symlink 语义

链接：<https://github.com/rcore-os/tgoskits/pull/449>

问题：StarryOS 的 `linkat` 对 unsupported flag bits 只是 warning，没有按 Linux 返回 `EINVAL`；同时默认路径解析会跟随最终 symlink，导致 `linkat(..., flags=0)` 没有按 Linux 语义硬链接 symlink 本身。

根因：`sys_linkat` 没有先严格校验 flags，而是把 flags 直接交给路径解析。路径解析的默认行为和 Linux `linkat` 的 no-follow 默认语义不一致。

修复：拒绝 unsupported flags；区分 `flags=0` 和 `AT_SYMLINK_FOLLOW`。前者链接 symlink 本身，后者链接 symlink target。

测试：新增 `bug-linkat-flags-symlink`，覆盖 invalid flags、symlink follow/no-follow、目标已存在和目录 hard-link 错误。

思考：这是典型文件 syscall 细节问题。应用层很少直接关心 link 本身，但构建系统、包管理器、兼容性测试会依赖 Linux 对 symlink 的精确约定。

### 4.2 PR #450：`mmap` fd 0 与 anonymous fd 语义

链接：<https://github.com/rcore-os/tgoskits/pull/450>

问题：StarryOS 旧 `mmap` 把 `MAP_ANONYMOUS` 和 `fd <= 0` 绑在一起，导致两个 Linux 兼容性错误：file-backed mmap 不能使用 fd 0；anonymous mmap 传入正 fd 时也会被错误拒绝。

根因：实现中用 fd 数值本身判断是否需要 backing file，而不是先按 `MAP_ANONYMOUS` 决定是否忽略 fd。

修复：file-backed mmap 允许 fd 0；`MAP_ANONYMOUS` 忽略 fd，但仍要求 offset page-aligned；非 anonymous 且 fd 为负时返回 `EBADF`。

测试：新增 `bug-mmap-fd-zero-anon`，覆盖 fd 0、anonymous fd handling 和错误路径。

思考：fd 0 通常是 stdin，但 Linux 允许它是一个普通打开文件。内核不能把“0 常见为 stdin”当成“0 不是有效 fd”。这类边界值对 POSIX/Linux 兼容性很重要。

### 4.3 PR #451：`renameat2(RENAME_NOREPLACE)`

链接：<https://github.com/rcore-os/tgoskits/pull/451>

问题：StarryOS 旧 `renameat2` 忽略 flags，导致 unsupported flags 没有返回 `EINVAL`，`RENAME_NOREPLACE` 也不能阻止覆盖已有目标。

根因：`sys_renameat2` 没有把 flags 纳入语义分支；新路径解析也按“不存在目标”路径处理，无法表达 `NOREPLACE` 对 existing destination 的约束。

修复：拒绝 unsupported flags；实现基本 `RENAME_NOREPLACE`；补齐 source missing 与 destination existing 的 errno 优先级；把 destination symlink，包括 dangling symlink，也视为 existing。

测试：新增 `bug-renameat2-noreplace-flags`，覆盖成功 rename、普通 replacement、invalid flags、errno priority、symlink destination。

边界：该 PR 声明覆盖顺序 syscall 行为，不宣称完整并发原子 `RENAME_NOREPLACE` 语义。

思考：这里体现了 PR 粒度控制：先补 Linux 可观察的单线程语义，避免把并发原子性这类更大问题混入同一 PR。

### 4.4 PR #460：`faccessat2` mode/flags 校验

链接：<https://github.com/rcore-os/tgoskits/pull/460>

问题：`faccessat2` 接受任意 mode 和 flag bits；legacy `faccessat` dispatcher 还会错误消费第四个 raw 参数作为 flags。

根因：实现没有在 path resolution 前做 Linux 风格的参数位校验；同时把 `faccessat` 和 `faccessat2` 分发路径合并过度，导致 legacy syscall 也读到了新 syscall 才有的 flags 参数。

修复：先拒绝 invalid access mode bits 和 unsupported flags；legacy `faccessat` 强制第四参数为 0。

测试：新增 `bug-faccessat2-validation`，覆盖 invalid mode bits、invalid flags、`AT_EMPTY_PATH`、symlink handling 和 legacy dispatch。

思考：这类 bug 的风险不只是返回值错误。如果非法参数在 path resolution 后才被拒绝，就可能产生不必要的路径访问、副作用或错误码优先级差异。

### 4.5 PR #461：`wait4` invalid options

链接：<https://github.com/rcore-os/tgoskits/pull/461>

问题：`wait4` 接受了 waitid-only 或 unknown option bits，例如 `WEXITED`、`WNOWAIT`，并且某些 invalid wait4 调用可能错误消费 child status。

根因：旧实现使用 `WaitOptions::from_bits_truncate`，会静默丢弃 unknown bits；还把 `WNOWAIT` 等不属于 wait4 的选项放进 wait4 路径处理。

修复：在扫描或回收 child 前拒绝 wait4 不支持的 option bits；从 wait4 path 移除 `WNOWAIT` 处理；invalid options 返回 Linux-compatible `EINVAL`，且不消费 child status。

测试：新增 `bug-wait4-invalid-options`，覆盖 `WEXITED`、`WNOWAIT`、unknown bits 和正常 child reaping。

思考：进程等待语义的关键不只是“能不能 wait 到 child”，还包括 invalid 参数不能改变进程状态。这个 PR 把“错误路径无副作用”也纳入回归。

### 4.6 PR #462：`fchmodat2` flags 校验

链接：<https://github.com/rcore-os/tgoskits/pull/462>

问题：`fchmodat2` unsupported flags 没有被拒绝，甚至可能在忽略 flags 后继续 chmod；legacy `fchmodat` 也错误消费第四个 raw 参数作为 flags。

根因：`sys_fchmodat` 没有先校验 flags；dispatcher 把 legacy/new syscall 的参数语义混在一起。

修复：在 path resolution 和 chmod side effects 前拒绝 unsupported flags；legacy `fchmodat` 强制 flags 为 0。

测试：新增 `bug-fchmodat2-flags`，覆盖 invalid flags、`AT_EMPTY_PATH` 和 legacy dispatch。

思考：这个 PR 和 #460 形成一类经验：`*at2` 新 syscall 往往比 legacy 版本多 flags 参数，dispatcher 不能简单复用而不清理参数。

### 4.7 PR #463：`readlinkat(size=0)`

链接：<https://github.com/rcore-os/tgoskits/pull/463>

问题：`readlinkat(..., size=0)` 在 StarryOS 中会成功返回 0；Linux 则返回 `EINVAL`。

根因：旧实现直接计算 `min(size, link.len())` 并写入这么多字节。size 为 0 时变成零字节写入并成功返回，没有单独处理 Linux 的 invalid zero-size buffer 语义。

修复：在路径解析前或写入前先拒绝 zero-size buffer，返回 `EINVAL`；保留短 buffer 截断、满 buffer 和非 symlink 行为。

测试：新增 `bug-readlinkat-zero-size`，覆盖 size 0、truncation、成功读取 symlink、普通文件 `EINVAL`。

思考：这是小而典型的边界值 PR。它的代码改动很小，但作为 Linux ABI，错误返回值会影响 libc、脚本工具和兼容性测试。

### 4.8 PR #464：`mknodat` mode type bits

链接：<https://github.com/rcore-os/tgoskits/pull/464>

问题：StarryOS 旧 `mknodat` 只接受显式 `S_IFREG` 创建普通文件；Linux 对 mode 中没有 `S_IFMT` type bits 的情况也按 regular file 处理。同时，`S_IFDIR` 情况应返回 `EPERM` 而不是 `EINVAL`。

根因：类型分发过窄，把 type-zero 误判为 invalid，也把 directory creation 这类 Linux 明确禁止但应返回 `EPERM` 的情况归入 `EINVAL`。

修复：把 mode type bits 为 0 的情况视为 regular file；`S_IFDIR` 返回 `EPERM`；保留 existing path、explicit `S_IFREG`、`S_IFLNK` 等路径语义。

测试：新增 `bug-mknodat-mode-type`，覆盖 type-zero creation、existing path `EEXIST`、explicit `S_IFREG`、`S_IFDIR`、`S_IFLNK`。

思考：`mknodat` 这类老 syscall 的 mode 参数有很多历史兼容细节。这里不能只按“类型字段必须显式存在”的直觉实现，要以 Linux 可观察行为为准。

## 5. 共性分析

Exp2 的 8 个 PR 可以归纳出几类共性问题。

第一类是 **flags/mode 校验过晚或过宽**。`linkat`、`renameat2`、`faccessat2`、`fchmodat2`、`wait4` 都涉及这个问题。Linux 往往要求先校验参数位，非法值应返回 `EINVAL`，而不是进入路径解析、权限检查或 child 扫描后再失败。

第二类是 **legacy syscall 与新 syscall 参数混淆**。`faccessat`/`faccessat2`、`fchmodat`/`fchmodat2` 都暴露了这个问题。复用实现时必须明确 legacy 版本没有 flags 参数，不能让 raw syscall 第四参数泄漏进新语义。

第三类是 **边界值与 errno 优先级**。`mmap(fd=0)`、`readlinkat(size=0)`、`mknodat(type=0)`、`renameat2` 的 missing source vs existing destination 都是例子。很多时候功能主路径看似可用，但边界值会暴露 ABI 不一致。

第四类是 **错误路径不能有副作用**。`wait4` invalid options 不能回收 child；`fchmodat2` invalid flags 不能 chmod；`faccessat2` invalid flags 不应先做不必要路径工作。这比“返回哪个 errno”更重要，因为它关系到应用状态是否被错误改变。

## 6. 验证方式

每个 PR 基本遵循同一验证模板：

1. 在 Linux 上用 `gcc`/`cc` 编译新增 C regression，确认 Linux 基准。
2. 运行 `git diff --check`。
3. 运行 `cargo fmt --check` 或 `cargo fmt --all -- --check`。
4. 运行 `cargo xtask clippy --package starry-kernel`。
5. 运行 StarryOS riscv64 QEMU bugfix case：

```bash
cargo xtask starry test qemu --arch riscv64 --test-group normal --test-case bugfix
```

这一套验证保证 PR 不只是“修当前现象”，而是把 Linux 行为固化成长期 regression。

## 7. 与后续实验的关系

Exp2 为后续 exp3/exp4 提供了两个基础经验：

1. **PR 拆分要足够小。** 每个 syscall 语义点单独提交，Reviewer 可以快速对照 Linux 行为和回归测试。
2. **真实应用问题最终会落到 syscall 细节。** 后续 BusyBox 和 Codex CLI 暴露的问题，本质上也大量来自 flags、errno、路径解析、进程状态、用户缓冲区等 Linux ABI 细节。

因此，Exp2 虽然看起来是一组零散 syscall bugfix，但它实际上验证了 pipeline 的工作方法：用小测例找出兼容性差异，用小 PR 固化修复，再把这些经验迁移到更大的应用兼容性任务中。

## 8. 总结

Exp2 的核心成果是：用 Codex pipeline 批量发现并修复了一批 StarryOS syscall Linux ABI 细节问题，并将它们拆成 8 个已合入的上游 PR。

这些 PR 覆盖了文件/目录、mmap、进程等待等基础语义。它们共同证明了一个方法论：对于类 Linux OS 来说，兼容性提升不能只看“大功能有没有”，还必须逐步补齐 flags、errno、边界值、副作用和 legacy syscall 分发这类可观察细节。Exp2 正是把这些细节从模型发现、Linux 差分、StarryOS 修复到上游合入跑通的一次集中验证。
