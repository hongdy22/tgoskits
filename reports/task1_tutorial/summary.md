# tg-arceos-tutorial 整体实验报告

日期：2026-05-07

## 一、整体理解

本项目是 ArceOS 教学实验集合。每个 `exercise-*` 目录都是一个可独立构建和运行的 crate，通过 `cargo xtask` 完成架构配置、交叉编译、镜像生成和 QEMU 启动。

这些实验覆盖了 ArceOS 的几个关键层次：

- 应用层：`src/main.rs` 中的测试程序和输出逻辑。
- 标准库适配层：`axstd` 在 `no_std` 环境中提供类 `std` 接口。
- 文件系统层：`axfs`、`axfs_ramfs` 负责 VFS 分发和 ramfs 实现。
- 内存管理层：`axalloc`、`bump_allocator`、用户地址空间和页映射。
- 系统调用层：`exercise-sysmap` 中的 Linux syscall 兼容实现。

整体思路是：先读 README 和代码框架，确认测试脚本检查的输出；再定位缺失能力所在的 ArceOS 层；最后尽量在本实验目录内通过本地 crate 或最小代码修改完成任务，并用脚本验证。

## 二、各实验完成方式

### 1. exercise-printcolor

目标是在串口输出中加入颜色。

完成方式：

- 修改 `src/main.rs`。
- 给 `Hello, Arceos!` 输出加入 ANSI SGR 控制序列。
- 使用 `\x1b[1;32m` 设置绿色加粗，使用 `\x1b[0m` 恢复默认样式。

关键点：

- 测试脚本不仅检查文本，还检查非 reset 的 ANSI SGR 序列。
- 因此只改变文字内容不够，必须实际输出颜色控制码。

### 2. exercise-hashmap

目标是在 ArceOS 的 `std::collections` 中支持 `HashMap`。

完成方式：

- 将 `axstd` 复制到实验目录作为本地 crate。
- 在 `Cargo.toml` 中把 `axstd` 改为 path 依赖。
- 在本地 `axstd` 的 `alloc` feature 中加入 `hashbrown`。
- 在 `axstd::collections` 中继续导出 `alloc::collections`，并额外导出 `hashbrown::HashMap`、`HashSet`。

关键点：

- `no_std + alloc` 环境中没有完整标准库集合类型。
- 直接使用 `hashbrown` 是比较合适的做法，因为它本身适合 `no_std` 场景。

### 3. exercise-sysmap

目标是实现用户态程序所需的 `mmap` 系统调用，使文件内容可以映射并读回。

完成方式：

- 在 `src/syscall.rs` 中实现 `sys_mmap`。
- 校验 `length`、`offset`、`fd`、`MAP_PRIVATE/MAP_SHARED` 等参数。
- 将 Linux 的 `PROT_READ/WRITE/EXEC` 转换为 ArceOS `MappingFlags`。
- 通过 `USER_ASPACE` 在用户地址空间中分配映射区域。
- 对文件映射，使用 `File::read_at` 从指定 offset 读取内容，再写入映射后的用户地址。
- 支持匿名映射和基本 `MAP_FIXED` 语义。
- 修改 `xtask/src/main.rs` 的工具链查找路径，使直接运行 `bash scripts/test.sh` 时能自动找到本机的 `riscv64-linux-musl-gcc`。

关键点：

- 这里涉及内核态和用户态边界，参数检查比普通应用实验更重要。
- 测试失败曾经不是 mmap 逻辑错误，而是测试脚本找不到 RISC-V musl 交叉编译器，因此需要让 `xtask` 自动查找本机工具链路径。

### 4. exercise-ramfs-rename

目标是在 ramfs 根文件系统中支持 `std::fs::rename`。

完成方式：

- 将 `axfs` 和 `axfs_ramfs` 复制到实验目录。
- 在 `Cargo.toml` 中使用 `[patch.crates-io]` 覆盖 crates.io 版本。
- 在 `axfs_ramfs/src/dir.rs` 中为 `DirNode` 实现 `rename`，支持同目录改名。
- 在 `axfs/src/root.rs` 中让组合根目录像 `create/remove` 一样正确转发 `rename`。
- 在公开 `root::rename` 中使用绝对路径，避免当前目录影响。

关键点：

- `std::fs::rename` 的调用链是 `axstd -> axfs -> axfs_ramfs`。
- 只改 ramfs 不够，根文件系统层也必须把 rename 分发到正确的 mounted fs。
- 本实验要求是 rename，不是跨目录 move，因此实现保持为同目录重命名。

### 5. exercise-altalloc

目标是实现 bump 风格内存分配器，并接入全局分配器。

完成方式：

- 在 `modules/bump_allocator/src/lib.rs` 中实现 `EarlyAllocator`。
- 实现 `BaseAllocator`、`ByteAllocator`、`PageAllocator` 三个 trait。
- 使用双端 bump 模型：
  - 字节分配从低地址向高地址增长；
  - 页分配从高地址向低地址增长；
  - 中间区域为剩余可用空间。
- 字节释放使用计数，计数归零后整体回收字节区。
- 页释放按照 bump 模型不做精细回收。

关键点：

- 该分配器同时承担 byte allocator 和 page allocator 的角色。
- `Vec` 压测会触发大量堆分配，必须正确处理对齐和剩余空间。
- bump 分配器简单高效，但释放能力弱，这是算法本身的限制。

## 三、值得关注的问题

1. 先沿调用链定位问题  
   这些实验大多不是只改应用层就能完成。例如 `std::fs::rename` 实际会经过 `axstd -> axfs -> axfs_ramfs`，`std::collections::HashMap` 实际依赖 `axstd` 的导出能力。调试时应先从测试入口出发，用 `rg` 查找函数和类型的调用链，再判断缺失能力在哪一层。

2. `no_std` 环境下很多能力需要显式补齐  
   在普通 Rust 程序里默认可用的 `HashMap`、文件系统接口、堆分配和系统调用，在 ArceOS 中都要由对应 crate 提供。遇到编译错误时不能只按桌面 Rust 的经验处理，而要看当前 crate 是否启用了 `alloc`、是否有本地 path 依赖、是否需要 `hashbrown` 这类 no_std 友好的实现。

3. VFS 操作要同时关注分发层和具体文件系统层  
   `exercise-ramfs-rename` 中如果只在 `axfs_ramfs` 实现 `rename`，根目录层仍可能返回 `Unsupported`。因此文件系统操作通常要检查两部分：上层 `axfs` 是否正确转发，下层具体文件系统是否真正实现。调试这类 bug 时，应关注错误来自 VFS 默认实现还是具体节点实现。

4. `mmap` 调试重点是参数、权限和地址空间  
   `exercise-sysmap` 的 `sys_mmap` 容易出错的地方包括长度对齐、offset 对齐、fd 合法性、`PROT_*` 到 `MappingFlags` 的转换，以及映射后是否把文件内容写入用户地址。调试时可以先确认 syscall 是否被调用，再检查返回地址是否有效，最后检查用户态程序读回的内容是否正确。

5. 分配器 bug 常出在边界和对齐  
   `exercise-altalloc` 的 bump allocator 看起来简单，但实际需要仔细处理 `Layout::align()`、页对齐、上下边界是否相撞、计数归零时是否回收字节区。分配器问题可能表现为启动失败、随机 panic 或大量 `Vec` 分配失败，因此实现后要用大规模分配测试压一下。

6. 本地 crate patch 要控制修改范围  
   多个实验需要复制本地 crate 并用 `[patch.crates-io]` 覆盖。这样做的好处是可以只影响当前实验，不污染全局依赖；但也要注意不要顺手大改无关代码，否则后续定位问题会困难。每次 patch 后应通过 `Cargo.lock` 或编译输出确认实际使用的是本地 crate。

## 四、测试结果

**所有测试在riscv64下全部通过**

`x86_64`、`aarch64`、`loongarch64` 因本机缺少对应 QEMU，在 `scripts/test.sh` 中自动跳过。
