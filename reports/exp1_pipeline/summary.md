# Exp1 StarryOS Codex 闭环 Pipeline 总结报告

日期：2026-05-08

## 1. 实验目标

本实验的目标是搭建一套可以驱动 StarryOS 自动迭代的 Codex-only pipeline。它不是单次调用模型写补丁，而是把“同步上游、开发、审查、提交、记录状态”拆成稳定的闭环：

1. Syncer 先把本地 `tgoskits` checkout 同步到上游最新 `dev`。
2. Developer 根据固定 prompt、journal 和 backlog 选择一个小目标，完成测试、差分、修复和验证。
3. Reviewer 独立复核 Linux 基准、StarryOS 行为、测试覆盖和补丁风险，输出 `PASS` / `REVISE` / `REJECT`。
4. Reviewer `PASS` 后由确定性 Committer 创建分支、提交、push，并把成果写入状态文件。
5. 下一轮从基线继续，避免在同一个 diff 上越堆越多问题。

这套机制最初服务于 StarryOS syscall/兼容性自动优化，当前目录里的配置已经演进到 `exp3_busybox` 分支，用于 BusyBox 兼容性实验；但它仍然体现了 exp1 的核心 pipeline 设计。

## 2. 报告资料目录

本报告目录位于：

```text
target/reports/exp1_pipeline/
```

已经整理的材料如下：

```text
target/reports/exp1_pipeline/
  summary.md                    # 本总结报告
  README.md            # /home/threetu33/os_biglabB_task2/README.md 的副本
  materials/
    config.json                 # pipeline 主配置
    prompts/                    # Syncer/Developer/Reviewer/共享目标 prompt
    hooks/                      # preflight、post_*、on_pass 确定性检查配置
    schemas/                    # 各角色 JSON 输出 schema
    skills/                     # linux-baseline、qemu-verify 等 playbook
    state/
      journal.md                # 已运行轮次记录
      passed_commits.json       # Reviewer PASS 后提交的分支和 commit
```

没有复制 `__pycache__`、大规模 `results/rounds` 运行产物、认证文件或 tgoskits 构建产物。

## 3. 实验环境与入口

核心目录：

```text
run.py
pipeline/
codex/
tgoskits/
```

其中：

- `run.py` 是唯一入口，只保留 `dry-run` 和 `loop`。
- `pipeline/config.json` 描述 tgoskits 路径、Codex binary/auth、默认架构、各角色模型配置、提交 remote 和分支前缀。
- `pipeline/prompts/` 保存固定 prompt 和角色规则。
- `pipeline/scripts/agent_loop.py` 是主调度器。
- `pipeline/results/state/` 保存跨轮次状态。
- `tgoskits/` 是被 pipeline 修改和测试的外部 checkout。

常用命令：

```bash
cd /home/threetu33/os_biglabB_task2
python3 run.py dry-run --max-rounds 1
python3 run.py loop --max-rounds 3
python3 run.py loop --max-rounds 3 --continue-after-pass
```

`dry-run` 只生成 prompt，不调用 Codex；`loop` 会真正调用 Codex 并推进 Syncer -> Developer -> Reviewer -> Committer 流程。

## 4. Pipeline 架构

整体架构可以概括为：

```text
固定规则与状态输入
        |
        v
Syncer Codex: 同步上游 dev
        |
        v
Developer Codex: 选题、写测试、做差分、修复、验证
        |
        v
Reviewer Codex: 独立复核证据链和风险
        |
        +-- REVISE/REJECT: 保留 diff，带意见继续下一轮
        |
        +-- PASS
              |
              v
        Committer: 确定性 commit/push/恢复基线/记录成果
```

这个设计里，AI 负责需要理解代码和语义判断的部分，脚本负责确定性的机械步骤：

- 准备 Codex binary 和 auth。
- 拼接 prompt。
- 执行角色调用。
- 校验 JSON schema。
- 保存 patch、diff stat、event log、summary。
- 检查 `git diff --check`。
- Reviewer PASS 后创建分支、提交并 push。

这样可以避免把提交、状态记录、路径检查这类机械动作交给模型自由发挥。

## 5. Prompt 和状态设计

每轮 prompt 由两部分组成：

```text
固定规则 + 本轮上下文
```

固定规则来自：

- `pipeline/prompts/syncer_role.md`
- `pipeline/prompts/developer_role.md`
- `pipeline/prompts/reviewer_role.md`
- `pipeline/prompts/shared_main.md`
- `pipeline/prompts/goal.md`
- `pipeline/prompts/strategy.json`

本轮上下文包括：

- round number
- 当前 branch / HEAD
- `git status --short`
- `journal.md` 最近轮次摘要
- `passed_commits.json` 已 PASS 项
- 上一轮 Reviewer 的 `PASS` / `REVISE` / `REJECT` 意见

prompt 不直接塞完整 `git diff`。Codex 需要上下文时自己运行 `git diff`、`rg`、`sed` 等命令读取仓库，这样 prompt 更小，也减少 stale context。

## 6. 结构化输出与确定性护栏

pipeline 用 JSON schema 限制各角色最终输出：

- `schemas/syncer.json`
- `schemas/developer.json`
- `schemas/reviewer.json`
- `schemas/committer.json`

同时用 hooks 做确定性检查：

- `preflight.json`：检查 `tgoskits`、StarryOS 路径、Codex auth、Codex binary/tarball。
- `post_syncer.json`：校验 Syncer 输出和同步后的工作区状态。
- `post_developer.json`：校验 Developer 输出，保存 patch，记录 `git diff --check`。
- `post_reviewer.json`：校验 Reviewer 输出，并确认 Reviewer 没有留下正式源码改动。
- `post_committer.json`：确认提交后分支、commit、工作区状态和空白检查。
- `on_pass.json`：生成本轮 summary 和 PR body 草稿。

这个设计解决了两个实际问题：

1. 模型自然语言输出不稳定，必须有 schema 让 Orchestrator 能稳定读取 `target`、`evidence`、`decision` 等字段。
2. 模型可以执行命令和写文件，但不应该自由决定是否提交、如何命名分支、如何恢复基线。

## 7. 测试与验证原则

pipeline 的核心工程原则是：

- Linux 行为是基准。
- 没有 Linux/StarryOS 差分证据，不算确认 bug。
- 没有长期回归测试，不算合格修复。
- 单轮只处理一个明确问题，或一组强相关语义。
- Reviewer 有否决权。
- Reviewer PASS 后才允许提交。

当前 BusyBox 配置里，Developer 必须围绕 issue #13 的三列建立证据链：

- `FAIL 测试`
- `测试命令`
- `验证方式`

修复后优先沉淀：

- BusyBox 脚本级回归：`test-suit/starryos/normal/qemu-smp1/busybox/sh/busybox-tests.sh`
- 单一源码级 C 回归：通常放入 `test-suit/starryos/normal/qemu-smp1/bugfix/<bug-name>/`

验证命令优先使用仓库原生入口：

```bash
cargo xtask starry test qemu --arch riscv64 --test-group normal --test-case busybox
cargo xtask starry test qemu --arch riscv64 --test-group normal --test-case bugfix
cargo fmt --check
cargo xtask clippy --package starry-kernel
git diff --check
```

## 8. 当前运行成果

这个 pipeline 后续分化出了多条实验线，本地 git 分支中可以看到代表性的 `exp2_debug` 和 `exp3_busybox`。因此 exp1 报告不展开每个具体 bugfix，只总结 pipeline 机制本身带来的工程产出：

- `exp2_debug`：面向 StarryOS syscall/debug 兼容性问题，基于这套闭环机制拆出了 8 个上游 PR，均已合入：[#449](https://github.com/rcore-os/tgoskits/pull/449)、[#450](https://github.com/rcore-os/tgoskits/pull/450)、[#451](https://github.com/rcore-os/tgoskits/pull/451)、[#460](https://github.com/rcore-os/tgoskits/pull/460)、[#461](https://github.com/rcore-os/tgoskits/pull/461)、[#462](https://github.com/rcore-os/tgoskits/pull/462)、[#463](https://github.com/rcore-os/tgoskits/pull/463)、[#464](https://github.com/rcore-os/tgoskits/pull/464)。
- `exp3_busybox`：面向 BusyBox applet/script 兼容性问题，基于同一套闭环机制拆出了 7 个上游 PR，均已合入：[#477](https://github.com/rcore-os/tgoskits/pull/477)、[#479](https://github.com/rcore-os/tgoskits/pull/479)、[#480](https://github.com/rcore-os/tgoskits/pull/480)、[#481](https://github.com/rcore-os/tgoskits/pull/481)、[#482](https://github.com/rcore-os/tgoskits/pull/482)、[#483](https://github.com/rcore-os/tgoskits/pull/483)、[#484](https://github.com/rcore-os/tgoskits/pull/484)。

这些 PR 说明 pipeline 不只是生成 prompt，而是可以稳定支撑“选题 -> 差分 -> 修复 -> review -> 分支提交 -> 上游合入”的完整流程。具体 syscall/debug 修复留到 exp2 报告展开，具体 BusyBox 修复留到 exp3 报告展开。

## 9. 代表性问题与经验

### 9.1 Reviewer 的价值

Reviewer 不是形式审查，而是闭环质量的关键部分。实际运行中，它多次发现“测试看似过了但语义仍错”的问题，例如返回码/errno 优先级、非目标 fd 类型语义、协议字段构造、回归脚本是否误弱化等。Reviewer 输出 `REVISE` 后，Developer 会在同一 target 上继续修订；只有 Reviewer `PASS` 才进入提交阶段。

### 9.2 Journal 和 passed commits 防止重复劳动

每轮 prompt 都会读取 `journal.md` 和 `passed_commits.json`。Developer 必须排除已 PASS 项，不能因为某个修复尚未合入 upstream/dev 就重复解决同一目标。

这个机制解决了自动循环常见的“模型忘记历史、重复做同一题”的问题。

### 9.3 Committer 必须确定性

最初如果让模型自己提交，很容易出现分支命名不一致、提交范围不清、忘记恢复基线等问题。当前设计把 Committer 作为脚本阶段：

- 记录本轮开始前 branch/HEAD。
- 按当前实验配置创建 `<branch-prefix>-<round>-<target>-<timestamp>` 分支。
- `git add -A`、commit、push。
- 切回本轮开始前的基线。
- 写入 `passed_commits.json` 和 `journal.md`。

这让下一轮不会依赖当前工作区残留 diff。

### 9.4 Prompt 要约束“不要贪多”

BusyBox backlog 很长，模型容易一次修很多项。当前 prompt 强调：

- 单轮只处理一个明确 applet。
- 只恢复本轮修复项对应的 BusyBox 检查。
- 不搬运其他 pending upstream 分支里的检查项。
- 若抽不出源码级 C 回归，必须解释原因。

这些约束让 PR 粒度更适合 review，也减少了互相污染。

## 10. 当前边界

当前 pipeline 已能支撑本地自动闭环，但仍有边界：

- 它不会自动打开上游 PR，PASS 后只是 push 到 fork 分支。
- 它无法替代人类最终审核；尤其是 Linux ABI、网络协议和并发语义仍需要人工抽查。
- `pipeline/results/rounds` 会随运行增长，报告只保留了状态摘要，没有复制每轮完整事件日志。

## 11. 总结

Exp1 pipeline 的核心成果是建立了一套可运行、可记录、可审查的 Codex 闭环开发机制。它把模型能力放在“理解代码、写测试、定位根因、生成补丁、复核语义”上，把提交、状态、schema 校验、hook 检查和历史记忆交给确定性脚本。

从后续 exp2 和 exp3 的结果看，这套机制已经能连续产出多个小粒度 StarryOS 兼容性修复，并通过 Reviewer 发现初版补丁中的真实语义问题。它的价值不只是自动写代码，而是把“测试先行、Linux 基准、最小修复、独立审查、自动沉淀”的工程流程固定下来。
