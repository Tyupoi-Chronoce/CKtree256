# AGENTS.md — CKtree256 (main)

> 适用分支：`main`（稳定线）。
> 最后更新：2026-11-16。
> 本文件是项目级代理约定，补充用户级 `~/.dsh/AGENTS.md`；两者重叠时，项目里更具体的要求优先，但 git 安全、隐私和破坏性操作三条不得放宽。
> 读它的时机：在本仓库改代码、跑测试、更新文档、拆分库或准备提交之前。

## 1. 项目是什么

CKtree256 是一个实验性的 **256 叉字节基数树**（byte-radix tree），C++20，单个开发源文件，命名空间 `CKtree`，主类 `Tree256`。长期目标是用“逻辑字节路径”代替跨分支的裸指针，并用 HVcache 把热点跳转压到 O(1)。

当前状态是 **pre-alpha structural prototype**，不是生产库。可以创建子节点数组、按路径读写/清除 `dataP`、按路径建立 link、按源路径解析 link、递归释放子树；但缓存淘汰还没有接入调用链，没有 pin、稳定节点句柄、路径失效检测、持久化、正式测试框架或构建系统。API 会变，**不要用于生产**。

分支关系：

- `main`：稳定线，本文件所在分支。
- `_with_template_dataType`：模板化实验线，允许更大胆的改动；不要把实验性改动直接搬到 `main`，除非用户明确要求并完成回归。
- 两个常用工作区：`D:\ProgramOBJs__Local\CKtree256_mainT`（main）和 `D:\ProgramOBJs__Local\CKtree256`（template）。先跑 `git worktree list` 和 `git status -sb`，不要在错误的工作区改错分支。

## 2. 文件所有权和事实来源

| 路径 | 角色 | 规则 |
|---|---|---|
| `classBasic.cpp` | **开发源，唯一逻辑事实来源** | 主要改动在这里；按第 3 节规则先做副本，原件由用户决定是否合并 |
| `_4lib_work/CKtree256.hpp` / `.cpp` | 从 `classBasic.cpp` 机械拆出的库格式 | 不把它当第二份手写源；源变化后重新生成/同步，并更新文件头的源 SHA |
| `lib/CKtree256.hpp` / `lib/libCKtree256.a` | 预编译 MinGW-w64 静态库 | 不手改；由 `_4lib_work` 重新构建；GCC 14.2.0 / MinGW-w64 ABI，其他 ABI 不要复用 |
| `smoke.cpp` | DSH 维护的 public API 冒烟测试 | 直接 include `classBasic.cpp`，不链接库；行为变化时补测试，不要削弱现有断言 |
| `testMain.cpp` | 用户自己的实验入口 | **DSH 不编辑**，除非用户在当前请求中明确要求 |
| `README.md` / `README.zh-CN.md` | 用户文档（中英双份） | 公开行为变化后两份都要同步；用户明确要求“更新 README”时可直接改 |
| `docs/C_API_design.md` | C ABI 设计草案 | 状态是“未实现”，不要把草案描述当作已实现行为 |
| `docs/threading_design_notes.md` | 后台淘汰线程的设计注意事项 | 同样是未实现草案；加线程前先按它核对生命周期和锁顺序 |
| `version.md` | 版本标记 | 只在用户要求发版/改版本时改 |
| `introduction.md` | 本地设计讨论 | 被 `.gitignore` 忽略，**不要提交、不要外发** |
| `classBasic_Copy4AW.cpp` | 被忽略的旧副本/临时副本 | 不是事实来源；不要把它当源文件继续改，也不要提交 |

## 3. 修改文件的硬规则

1. **先读后改。** 不改没读过的文件；不凭记忆猜当前实现。
2. 修改**已存在**的项目文件前，先在旁边创建 `<原名>_Copy4AW.<后缀>`，所有编辑只作用在副本上；原件不动，是否合并由用户决定。新建文件、`.dsh/` 内文件不适用。
3. 副本合并后（或用户自行合并、内容一致后）默认删除副本，不要长期堆 `_Copy4AW` 文件。
4. 直接编辑的例外只按用户当前请求判断：用户明确说“更新 README”时可以改 README；`testMain.cpp` 属于用户，默认不动。`smoke.cpp` 虽然由 DSH 维护，仍是既有项目文件；除非当前请求明确要求改 smoke，否则先副本或先确认。
5. **不执行 commit / push / 建远程库 / `reset --hard` / `filter-repo` / 强推 / 删分支**，除非用户在当前回合明确委托。代做前先备份，报告里写明“已代做 / 未推送 / 改动未提交”。
6. 破坏性操作（删除、移动、改名）先核对绝对路径，优先可还原方式；不清理来源不明的历史文件。
7. 不引入第三方依赖、构建系统、CMake、CI 或新线程模型，除非任务明确要求；当前保持“单文件源 + 手写拆分 + 静态库”的形态。
8. 临时探针、`.o`、`.exe`、测试输出放到临时目录或已忽略位置，不留在源码树里。

## 4. 构建和验证

本机工具链是 w64devkit 的 GCC 14.2.0；`.vscode/tasks.json` 指向 `C:\mingw64\w64devkit\bin\g++.exe`。命令行用 `g++` 即可。**所有 C++ 改动必须在 `-Werror` 下零警告通过。**

### 4.1 单文件源与 smoke

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -c classBasic.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror smoke.cpp -o smoke
./smoke
```

smoke 的预期输出必须恰好包含这两行，并以 0 退出：

```text
[smoke] put_data / get_data / erase_data / createChildren / link_node / get_linkNode / free_nodeTree OK
[smoke] target-side hitCounts_4beJumped admission, cache hit, relink and cached-target free OK
```

`testMain.cpp` 只做编译/运行检查，不是测试覆盖来源：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror testMain.cpp -o testMain
./testMain
```

### 4.2 库格式拆分和静态库

源码改动后，先确认 `_4lib_work` 已同步，再重建库：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

consumer 验证：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Ilib app.cpp lib/libCKtree256.a -o app
./app
```

### 4.3 本机限制

- **没有 ASan**（`-lasan` 不可用），不要声称跑过 AddressSanitizer。
- 访问冲突可能被 Windows WER 挂起而不是立刻返回；写探针时先安装 `SetUnhandledExceptionFilter` / `SetErrorMode(SEM_NOGPFAULTERRORBOX)`，才能稳定拿到 `0xC0000005`。
- 没有正式测试框架、CMake、CI；不要编造“测试全部通过”。证据只能是实际命令、退出码和输出。
- 过滤器 `filter.stripws` 只在当前 repo-local 配置里生效；其他 clone 不保证自动去尾空格。

## 5. 当前 API 和不可破坏的契约

public API（以当前 `classBasic.cpp` 为准）：

```cpp
Tree256(size_t HVcache_size = HVcache_minSize); // 当前不是 explicit
bool createChildren(uint8_t* nodePathP, size_t length);
bool put_data(uint8_t* nodePathP, size_t length, void* dataGP);
void* get_data(uint8_t* nodePathP, size_t length);
bool erase_data(uint8_t* nodePathP, size_t length);
bool link_node(uint8_t* dst_nodePathP, size_t from_dstNodePath,
               uint8_t* linkPathP, size_t length_linkedPath);
treeNode* get_linkNode(uint8_t* nodePathP, size_t length);
bool free_nodeTree(uint8_t* nodePathP, size_t length);
```

构造函数当前不是 `explicit`，`Tree256 tree = 1024;` 这类隐式转换在语言上可行；这不是团队约定，只是现状。

契约：

- **路径 = 字节序列 + 显式长度**。`nullptr + 0` 表示根；`nullptr + length > 0` 非法；`0x00` 是普通字节，不是终止符；`{'A'}` 和 `{'A','B'}` 是不同节点。
- `dataP` 是**非拥有**指针：树从不 delete 它；调用方负责生命周期。重复 `put_data` 只覆盖指针，不释放旧数据；`erase_data` 只清空 `dataP`，不删除节点、不剪枝。
- `get_data` 无法区分“路径不存在”和“节点存在但没有 data”，两者都返回 `nullptr`。
- `link_node` 要求源节点和目标节点都已存在；目标路径会被复制（可能抛 `std::bad_alloc`）；重复 link 会替换旧路径。
- `get_linkNode(path, len)` 的 `path` 是**源节点**路径；返回目标节点的裸 `treeNode*`。它不是稳定句柄，目标被 `free_nodeTree` 后立即失效；缓存命中时返回目标节点本身，不返回槽地址。
- `free_nodeTree` 递归释放子树、每个节点复制的 external path 和它自己的缓存槽；**不释放 dataP**；**不允许释放根**；键很深时递归深度等于键长，作者已知并接受栈溢出的风险。
- 公开 `bool` API 里，`link_node` 的路径复制会抛 `std::bad_alloc`；`createChildren` / `put_data` 的分配也可能抛。需要 no-throw 的调用方必须自己 catch。

## 6. 缓存、链接和生命周期的现状

- HVcache 是**非拥有**的指针表：`size0f_HVcache + 1` 个槽，槽 0 是哨兵；最小缓存 0xff 个槽。
- 缓存准入按**目标节点**的 `hitCounts_4beJumped` 判断，阈值 `minCounts_move2cache = 0x0f`；三个命中分支都要饱和递增，不能回绕。
- 槽有 generation；源节点记录 `linkNode_cacheGeneration`，缓存命中要同时校验索引、generation、目标节点自身的 `self_in_cache` / `self_cacheIndex`。槽失效时清源记录并回退到路径查找。
- `_cache_eviction(bool sweep, bool hit_downrade, uint8_t penalty)` 已有 sweep、惩罚和“满表时淘汰最少命中”的兜底，**但当前没有任何调用方**；后台 worker 也没有实现。不要声称“缓存淘汰已经可用”。
- 没有 pin/引用计数；缓存里的节点不能被直接 delete。`free_nodeTree` 会先 `_remove_self_from_cache`，但当前没有“节点正被外部使用”的保护。
- 路径没有 revision/hash 校验；结构变化后旧路径可能静默指向另一个节点。不要把它描述成“零悬垂指针已完全实现”。
- 没有稳定的 public 节点身份/句柄；`get_linkNode` 的返回值只在目标节点存活且调用方保证不被并发释放时有效。

## 7. 锁和线程

- `privateMutex` 保护根指针、树结构和 HVcache 槽表。
- public 入口每次调用只加一次锁；private helper（`_find_node`、`_node_isExist`、`_mount_node`、`_create_childrenArray`、`_can_putInto_cache`、`_put2cache`、`_get_linkNode`、`_cache_eviction`、`_remove_self_from_cache`、`_forget_link_cache`、`_free_nodeTree`）要求调用方已经持锁。
- 不要在 private helper 里再锁一次；不要在持锁时调用会自己加锁的 public 方法。
- 析构函数持锁后递归 `_free_nodeTree`；递归本身不再加锁。
- 将来加淘汰线程时，必须按 `docs/threading_design_notes.md`：构造最后启动 worker，析构先 stop/notify/join 再拿锁，worker 只调用持锁 private helper，异常不得逃出线程函数。**不要先实现线程再补锁顺序。**

## 8. 文档、拆分和库的同步

改变了 public 行为或内部关键逻辑后，按以下顺序检查：

1. `README.md` 和 `README.zh-CN.md`：API、状态、限制、构建命令、TODO 是否仍准确；两份要同步。
2. `smoke.cpp`：现有断言是否仍成立；新增行为是否有最小覆盖；不要删掉已有场景。
3. `_4lib_work/CKtree256.hpp` / `.cpp`：从 `classBasic.cpp` 同步类声明和实现；文件头的源 SHA 要更新。
4. `lib/CKtree256.hpp` 和 `lib/libCKtree256.a`：按 4.2 重建；consumer 重新跑。
5. `docs/C_API_design.md`、`docs/threading_design_notes.md`：它们引用的行号和源 SHA 会漂移；至少更新基线说明，不要留“当前源码”的旧哈希。
6. `version.md`：只在用户要求发版时改；不要把未验证的改动写成新版本。

文档里的行号只是辅助，**引用前重新读源码核对**；不要凭旧行号下结论。

## 9. Git 和工作区

- 先跑 `git worktree list` 确认当前是 main 还是 template 工作区；不要在 main 工作区里 `git switch` 到 template，反之亦然。
- 默认**不 commit / 不 push / 不 merge / 不 rebase / 不 amend 已推送提交 / 不 force push**；只有用户当前回合明确委托才做，并在报告里写清楚状态。
- 提交前检查 `git status --short`、`git diff --cached --stat` 和 `git diff --cached --check`。VSCode 默认“Commit”只提交 staged；不要因为按了 Commit 就以为工作区所有改动都进去了。
- 只 stage 本次请求相关的文件；不要顺手 `git add -A` 把无关改动、临时探针或旧副本带进去。
- `.gitattributes` 的 `filter.stripws` 会去 C++ 源文件尾空格；`*.md` 设置了 `whitespace=-trailing-space`，Markdown 两空格硬换行合法且不会被 `git diff --check` 报错，不要为了“干净”把有意义的硬换行删掉。
- 不删除未知历史文件；不覆盖别人的未提交改动；遇到 `FS_STALE_VERSION` 先重读再重试。

## 10. 代码风格和改动边界

- 保持现有风格：`Tree256`、`treeNode`、private helper 前缀 `_`，C++20，单文件源；不要为了“现代化”整体重写。
- 只做任务要求的最小改动；不顺手重命名、不顺手改格式、不顺手重构无关函数。
- 注释要解释“为什么”和生命周期/锁前提，不只复述代码；现有英文注释可以保留，回复用户用中文。
- 新增 public API 前先确认：路径语义、data 所有权、失败返回、锁、缓存/句柄生命周期是否说得清。说不清就先写设计，不要直接加接口。
- 新增测试优先放 `smoke.cpp`（如果任务允许改 smoke）或临时探针；`testMain.cpp` 留给用户。

## 11. 当前已知缺口（不要把 TODO 当已完成）

- 缓存淘汰没有调用方；没有后台 worker、没有 pin、没有淘汰的直接测试。
- 没有稳定节点句柄；`get_linkNode` 返回裸指针，`free_nodeTree` 后悬垂。
- 没有路径 revision/hash 校验；结构变化后旧路径可能指向错误节点。
- 没有持久化；`childFP` 仍是裸指针，不能直接 dump。
- 每个节点固定 256 个指针（约 2 KB），稀疏树浪费明显；bitmap + compact array / ART 布局仍待做。
- `tempNodeP_4easyFind` 仍是隐式共享状态；`_find_node` 的重复遍历是已知问题。
- 没有并发测试、正式测试框架、CMake、CI；本机没有 ASan。
- `_4lib_work` / `lib` / docs 的源哈希可能落后于 `classBasic.cpp`；改源后必须重新核对。

## 12. 交付和汇报

被要求“看一下/审查”时：**只读**，不要改文件；结论先行，列 P0/P1/P2，给 `文件:行号`、命令输出或哈希证据，明确哪些没验证。

被要求“改一下/实现”时：按第 3 节副本规则做；完成后报告：

1. 结论（改了什么、是否通过）；
2. 证据（构建/运行命令、退出码、关键输出、文件哈希/行号）；
3. 遗留隐患；
4. 未验证项；
5. 下一步/需要用户决定的事。

没有证据就不要写“已完成”。不要用“应该是/大概/可能”代替实际测试；不确定就写“未验证”。

---

最后提醒：这是用户长期维护的实验项目，`classBasic.cpp` 是唯一源。优先保持它能编译、能跑 smoke、边界条件说得清，再谈缓存、线程和持久化。