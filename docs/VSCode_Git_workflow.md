# CKtree256：VSCode 图形化 Git 操作注意事项

> 适用：VSCode 内置 **Source Control（源代码管理）**，Windows 环境。
>
> 写作基线：2026-11-14，HEAD `d08cd08 v0.1.7T`；`main` 与 `_with_template_dataType` 同点。写作时工作区并非 clean（例如 `M classBasic.cpp`、`?? docs/VSCode_Git_workflow.md`）；本文的 clean 检查是操作要求，执行前以 `git status` 为准。
>
> 目标：把“切分支前 clean + synced”变成可执行检查，避免切错分支、漏推、误丢弃、CRLF 整文件 diff。

## 0. 一句话原则

**VSCode 的 Git GUI 只是 git 的薄壳：它负责方便，不负责替你保证 clean + synced。**

按钮点下去之前，先确认两件事：

- **clean**：`Changes` 和 `Staged Changes` 都为空；未跟踪文件也算 `Changes`，被 `.gitignore` 忽略的文件不会显示。
- **synced**：状态栏没有 `↑` / `↓`，或 `git status -sb` 的 `##` 行没有 `[ahead N]` / `[behind N]`。

---

## 1. 按钮对照：VSCode 到底执行了什么

| VSCode 操作 | 实际动作 | 关键点 |
|---|---|---|
| 源代码管理里的 `+` / Stage Changes | `git add` | 只暂存，不提交 |
| 对勾 **Commit** | `git commit` | 默认只提交 Staged；`git.enableSmartCommit` 默认 false |
| 提交按钮下拉里的 **Commit & Push**（有的版本提供） | commit + push | 只推，不 pull |
| **Sync Changes / 同步更改** | pull + push | **不是单纯 push**；落后时会先 pull，可能产生合并 |
| 状态栏分支名 | `git switch` | 不强制 clean；草稿、暂存区、未跟踪文件都可能跟过去 |
| **Fetch** | `git fetch` | 只更新远端引用，不动工作区，安全 |
| **Pull** | `git pull` | 会改工作区，可能冲突 |
| **Publish Branch** | 首次 push + 设置 upstream | 只在新分支没有 upstream 时出现 |
| **Discard Changes / 放弃更改** | `git restore` 或删除未跟踪文件 | **不可逆**；未提交内容没有 reflog |
| **Stash** | `git stash` | 只存在本地，不会推送 |
| Source Control Graph 右键 Reset / Rebase / Delete | 真实的 git 操作 | 不是预览；确认目标再点 |

---

## 2. 切分支前：60 秒检查清单

### 2.1 看 Source Control

按 `Ctrl+Shift+G` 打开源代码管理：

- `Changes` 应为 0 条；
- `Staged Changes` 应为 0 条。

注意：

- **未跟踪文件属于工作区，不属于任何分支**。切到另一个分支后它仍然在，会继续出现在 `Changes` 里。
- `.gitignore` 中的 `test/*`、`*_Copy4AW*`、`.dsh/*`、`introduction.md` 不会出现在 Source Control；不要用“界面干净”推断这些文件不存在。
- 如果确实要让 `test/` 里的文件入库，需要 `git add -f test/xxx` 或修改 `.gitignore`，GUI 不会代劳。

### 2.2 清空提交输入框

VSCode 的提交输入框是**仓库级**的，不是分支级；切换分支不会清空草稿。

如果带着“给 feature 写的提交说明”切到 `main`，下一次提交很容易把说明和改动一起提交到错误的分支。**切之前要么提交，要么清空输入框。**

### 2.3 看状态栏的 `↑` / `↓`

- `↑ N`：本地有 N 个提交没推送；
- `↓ N`：远端有 N 个提交没拉下来；
- 两个都有：本地和远端已经分叉。

`git.autofetch` 默认是 false，箭头可能过期。点一下 **Fetch**，或终端执行 `git fetch`，再看一眼。

处理顺序：

1. 有未提交改动：先 commit 或 stash；
2. 有 `↑`：点 **Sync Changes**；
3. 有 `↓`：先 **Pull**，再决定是否推送；
4. 两个都有：先 commit，再 Sync；出现冲突就解决后提交。

### 2.4 选本地分支，不选 `origin/xxx`

点状态栏分支名，VSCode 会弹出分支选择器：

- 优先选本地分支名：`main`、`_with_template_dataType`；
- `origin/main`、`origin/_with_template_dataType` 是远程分支；选中它可能新建一个本地跟踪分支，容易多出分支或切到非预期状态。

如果只是想切到已经存在的本地分支，**不要点远程名**。

另外：在当前分支有未提交改动时新建分支，改动会被带到新分支。想从干净状态开始，先提交或 stash。

### 2.5 切完立即验证

在集成终端执行：

```bash
git status -sb
```

期望第一行类似：

```text
## _with_template_dataType...origin/_with_template_dataType
```

并且下面没有文件列表。然后跑一次 smoke：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic smoke.cpp -o smoke.exe
./smoke.exe
```

### 2.6 如果 VSCode 提示 “Please clean your repository working tree”

这是 Git 拒绝切换，因为本地改动会被目标分支覆盖。处理方式只有：

- 提交到当前分支；
- 或 stash；
- 或把改动备份到仓库外后删除。

**不要点 Discard。** 确认框不是撤销，未提交改动删除后没有 reflog。

---

## 3. 提交与推送

### 3.1 提交前

1. 用 `+` 暂存要提交的文件；
2. 点开 `Staged Changes` 里的文件，看实际 diff，不要只看文件列表；
3. 终端执行：

```bash
git diff --cached --check
```

这一步抓行尾空白和 CRLF 问题，VSCode 的 diff 视图不一定能看出来。

4. 看 VSCode 右下角的行尾标记：当前仓库是 **LF**，应显示 `LF`。如果显示 `CRLF`，点它改成 `LF` 再保存；否则 `classBasic.cpp` 这种大文件会整文件变成改动。
5. 确认提交输入框里没有上一个分支留下的草稿。

### 3.2 提交

- 默认 `Commit` 只提交 **Staged** 的内容；
- `git.enableSmartCommit` 默认 false；如果你开启过 Smart Commit，在没有暂存时它会自动暂存并提交所有改动，容易把临时文件带上；
- 不要用 `Commit All` 代替检查 `Staged Changes`；
- 提交信息写清楚“做了什么、为什么”，不要只写 `update`。

### 3.3 推送

- **Sync Changes** = 需要时 `git pull` + `git push`，不是纯 push；
- 当前 `git.rebaseWhenSync` 默认 false，**不要打开**；打开后 Sync 会用 rebase，已推送的分支可能被迫 force push；
- 当前 `git.autoStash` 默认 false，**不要打开**；打开后 pull 前会自动 stash，出问题更难查；
- 新分支第一次推送时，VSCode 显示 **Publish Branch**，点它；已有 upstream 的当前分支显示 **Sync Changes**；
- GUI 的 Push / Sync 只推**当前分支**。要在 feature 上推 `main`，用终端：

```bash
git push origin main
```

### 3.4 失败与冲突

- 推送失败先看 **Output → Git**；toast 只给一句，真正原因在 Git 输出里；
- 常见原因是远端领先：先 `git fetch`，再 `git pull`，解决冲突后再 push；
- 不要因为 push 被拒就直接 force push；
- 合并冲突时 Source Control 会显示 Merge Changes；解决后 stage + commit，不要点 Discard。

### 3.5 想少点一次：Commit & Push

有的 VSCode 版本在提交按钮下拉里提供 **Commit & Push**。如果没有，可以设置：

```json
"git.postCommitCommand": "push"
```

它会在每次成功 commit 后自动 push，**不会 pull**。不建议设成 `"sync"`，因为那会在提交后自动 pull，冲突和合并都可能在你没看着时发生。

---

## 4. 不要点的按钮 / 不要开的设置

| 操作 | 风险 | 替代做法 |
|---|---|---|
| **Discard Changes / 放弃更改** | 未提交内容没有 reflog，删了就是删了 | 提交、stash，或先备份到仓库外 |
| **Undo Last Commit** | 改写当前分支；已推送时会造成分叉 | 未推送可用；已推送用 `git revert` 新建反向提交 |
| **Commit (Amend)** | 改写已有提交；已推送后需要 force push | 已推送不要 amend；新增提交说明修正 |
| **Rebase Branch** | 改写提交历史；已推送分支会迫使 force push | 两个分支之间用 merge |
| **Force Push** | 覆盖远端历史，可能丢别人的提交 | 默认关闭；确实需要时先备份，并用 `--force-with-lease` |
| **Reset --hard** | 丢弃工作区和暂存区改动 | 用 `git reset --soft` / `--mixed`，或先 stash |
| **Delete Branch** | 未合并提交会丢 | 先看 `git log main..分支`，确认已合并再删 |
| 在 dirty tree 上点 **Sync** | 可能自动合并/自动 stash，结果不可预期 | 先 commit 或 stash，再 Sync |

---

## 5. 推荐设置

### 5.1 建议值

这些大多是默认值，重点是确认没有被改过：

```json
{
  "git.confirmSync": true,
  "git.rebaseWhenSync": false,
  "git.enableSmartCommit": false,
  "git.autoStash": false,
  "git.pullBeforeCheckout": false,
  "git.allowForcePush": false
}
```

说明：

- `git.confirmSync`：Sync 前弹确认；默认 true，建议保留。
- `git.rebaseWhenSync`：Sync 时强制 rebase；默认 false，已推送分支不要开。
- `git.enableSmartCommit`：没有暂存时自动提交全部；默认 false，建议保留。
- `git.autoStash`：pull 前自动 stash；默认 false，建议保留。
- `git.pullBeforeCheckout`：切到“没有 outgoing commits”的分支前先 fast-forward；默认 false，避免分支位置被悄悄改变。
- `git.allowForcePush`：允许 force push；默认 false，不要开。

### 5.2 可选值

按需选，合并进 `settings.json`：

```json
{
  "git.postCommitCommand": "push",
  "git.branchProtection": ["main"],
  "git.autofetch": true,
  "files.eol": "\n"
}
```

- `git.postCommitCommand`：每次提交后自动 push；用 `"push"`，不要用 `"sync"`。
- `git.branchProtection`：保护 `main`；提交到 `main` 前会按 `git.branchProtectionPrompt`（默认 `alwaysPrompt`）弹确认，能抓住“切错分支”。
- `git.autofetch`：只自动 fetch，不 pull；默认 false，想让 `↑`/`↓` 更及时可以开。
- `files.eol`：固定 LF。放用户设置最省事；放工作区会生成 `.vscode/settings.json`，是否入库由你决定。`git ls-files --eol` 显示当前已跟踪的源码/文本文件都是 `i/lf w/lf`；`lib/libCKtree256.a` 是二进制、`version.md` 是 `i/none`，不在这个结论里。

---

## 6. 本仓库的双版本日常流程

### 6.1 分支角色

- `main`：稳定版；
- `_with_template_dataType`：模板化数据类型实验版。

写作时两者都在 `d08cd08 v0.1.7T`，还没有分叉；第一次分叉后，按下面的流程走。

### 6.2 推荐流程

```text
feature 做模板实验
  → Stage → Commit → Sync
  → 切 main
  → 改稳定版 → Stage → Commit → Sync
  → 切回 feature
```

每次切换前，按第 2 节的 5 条检查。

### 6.3 冲突预防

- 两边都改 `classBasic.cpp`，以后合并一定容易冲突；
- 模板实验尽量放新文件：`tree256_template.hpp`、`template_smoke.cpp`；
- 稳定版只修 `classBasic.cpp` 的 bug，实验版只加模板文件，冲突面会小很多；
- 合并前先跑两边的 smoke，不要用“看起来没冲突”代替测试。

### 6.4 同时开两个版本：worktree

一个分支只能在一个工作树里 checkout。想同时开两个 VSCode 窗口：

```bash
git switch main
git worktree add ../CKtree256-template _with_template_dataType
```

然后在 VSCode 里打开 `../CKtree256-template` 这个文件夹。两个窗口各自有 Source Control，但共享同一个 `.git`；不要在两边同时 checkout 同一个分支。

---

## 7. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 切分支后，新文件还在 | 未跟踪文件属于工作区，不属于分支 | 提交、stash 或删除；不要以为切分支会带走它 |
| 切分支后，Staged 里还有东西 | 暂存区是仓库级的，Git 能带就带 | 先 commit 或 unstage，再切 |
| 整个文件都变红/绿 | 行尾从 LF 变成 CRLF | 右下角改回 LF；`git ls-files --eol` 确认 |
| Sync 后多了一个 merge commit | 本地有提交、远端也有提交 | 正常合并结果；看 Graph 确认，再继续 |
| Push 被拒绝 | 远端领先 | `git fetch` + `git pull`，解决后再 push；不要 force |
| Commit 带上了不想提交的文件 | Smart Commit / Stage All | 未推送可 amend；已推送用 revert 或新提交修正 |
| 分支列表出现 `origin/xxx` | 远程分支 | 优先选本地分支；远程名用于创建跟踪分支 |
| 提交后才发现切错分支 | 切分支时没检查 | 未推送可 `git switch` 后 cherry-pick；已推送不要改写，追加修复提交 |

---

## 8. 终端兜底命令

GUI 和终端操作的是同一个仓库；GUI 只给你看它想让你看的，终端是最终事实。

```bash
# 当前分支、ahead/behind、工作区状态
git status -sb

# 暂存区空白/CRLF 检查
git diff --cached --check

# 已跟踪文件的行尾
git ls-files --eol classBasic.cpp .gitignore README.md smoke.cpp testMain.cpp

# 所有本地分支的上游和 ahead/behind
git branch -vv

# 更完整的分支跟踪状态
git for-each-ref --format='%(refname:short) %(upstream:track)' refs/heads

# 最近历史图
git log --oneline --graph --all -n 20
```

`git status -sb` 的第一行如果是：

```text
## _with_template_dataType...origin/_with_template_dataType
```

说明当前分支有 upstream，且没有 ahead/behind。出现 `[ahead 1]`、`[behind 2]` 就先处理，再切分支。

---

## 9. 切分支前检查清单（可打印）

- [ ] `Changes` 为 0
- [ ] `Staged Changes` 为 0
- [ ] 提交输入框为空
- [ ] 已 Fetch，状态栏没有 `↑` / `↓`
- [ ] 选的是本地分支，不是 `origin/xxx`
- [ ] 切完 `git status -sb` 无文件
- [ ] smoke 通过
- [ ] 右下角行尾是 `LF`

---

## 10. 文档维护

VSCode 版本不同，按钮名称和位置可能略有差异；以本文的 **git 命令语义**为准，不要只记按钮位置。更新本文时同步修改顶部的“写作基线”日期和 HEAD。
