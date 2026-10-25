# CKtree256

An experimental **256-way byte-radix tree**. The long-term goal is a persistent, cross-process, dangling-pointer-free way to reference nodes inside and across branches: use **logical byte paths** instead of raw pointers, and a **hot-path cache** to flatten cross-branch jumps to O(1).

一个实验性的 **256 叉基数树**（byte-radix tree）实现。长期目标是给树内/跨分支的节点引用找到一种「可持久化、跨进程、无悬垂指针」的表达方式：用**逻辑字节路径**代替裸指针，并用**热路径缓存**把跨分支跳转摊平成 O(1)。

**Languages / 语言:** [中文](#中文) · [English](#english)

---

## 中文

### 项目简介

长期设想见设计文档 `introduction.md`（暂未公开，见 `.gitignore`）。本仓库目前只包含结构性原型和一组 API smoke test。

> **状态：pre-alpha，结构性原型。**
> 目前能创建子指针数组、按字节路径读写 `dataP`、递归释放整棵树；缓存只有「槽表 + 把目标节点放进空槽」的雏形。
> **还没有**跨分支跳转、缓存淘汰与 pin、持久化、单元测试框架和构建系统；`free_node` 仍是占位。
> 接口会大改，**不要用于生产**。

### 目录

| 文件 | 说明 |
|---|---|
| `classBasic.cpp` | 全部实现。目前是「头文件式 .cpp」（无 `main`、无 include guard），后续会拆成 `tree256.hpp` + `tree256.cpp` |
| `testMain.cpp` | API smoke test（root/空 path、put/get、前缀与嵌入 0、覆盖/非法参数、createChildren/free_node 回归），见「编译与运行」 |
| `introduction.md` | 设计文档，本地保留、未入库 |
| `LICENSE` | MIT 许可证 |

### 目前已经能做什么

- `createChildren(path, len)` —— 给路径上的节点分配 256 槽子指针数组（幂等；路径不存在时安全返回 false）
- `put_data(path, len, dataP)` —— 沿路径确保节点存在并写入 `dataP`；同一路径重复写入只覆盖指针，不释放旧 data；`dataP` 必须非空，树不拥有 data
- `get_data(path, len)` —— 按路径读取 `dataP`；路径不存在或节点没有 data 时返回 `nullptr`；`length == 0` 读根节点
- `_mount_node(path, len)` —— internal；单遍确保路径上的节点存在，返回目标节点的槽 `treeNode**`，不写 data；调用方持锁
- `_find_node(path, len)` / `_node_isExist(path, len)` —— internal 查找；`_find_node` 返回父节点里指向目标节点的槽指针（根节点返回 `&rootNodeP`）
- `_free_nodeTree(&slot)` —— 递归释放子树：子指针数组、external path、节点本体都释放；不释放 `dataP`，叶子不漏（实测 3 层树 + 外链路径，new/delete 计数 10/10 平衡）
- `HVcache_nodeP` / `HVcache_aliveState` —— `size + 1` 个槽的 non-owning 指针表，槽 0 为哨兵；`_can_putInto_cache` / `_put2cache` 可以把 external link 的目标节点放进空闲槽
- `privateMutex` —— 保护树与缓存；`createChildren`、`put_data`、`get_data`、`_put2cache`、析构函数内部加锁，其它 private 辅助函数要求调用方持锁（见「线程与锁约定」）
- `free_node(path, len)` —— public 占位接口，目前固定返回 false，尚未实现（见 TODO）

### 编译与运行

没有构建系统；`classBasic.cpp` 目前是「头文件式」实现，`testMain.cpp` 直接 `#include` 它。

```bash
# 只编译实现（检查是否能过编译）
g++ -std=c++20 -Wall -Wextra -Wpedantic -c classBasic.cpp -o classBasic.o

# 编译并运行 API smoke test
g++ -std=c++20 -Wall -Wextra -Wpedantic testMain.cpp -o ckt_test
./ckt_test
```

预期输出：

```text
[test] put_data / get_data / createChildren / free_node OK
[test] TODO: external path 跳转、HVcache 准入淘汰、free_node 仍未实现
```

当前版本在 GCC 14.2.0 / MinGW-w64 上编译零警告，test 退出码 0。

### TODO / 已知问题

按优先级排列，欢迎认领：

1. **接口未定型**：public 现在有 `createChildren`、`put_data`、`get_data` 和占位 `free_node`；还没有 `erase` / 子树删除，`free_node` 的语义（删除单个节点还是整棵子树、data 是否释放、外链和缓存如何失效）待定。
2. **key 与数据契约**：`put_data` / `get_data` 已用显式 length，支持嵌入 0；但外部路径格式仍需要长度或终结符约定，`dataP` 所有权（当前约定树不 delete，调用方负责）也要最终写死。`get_data` 目前不能区分「路径不存在」和「节点存在但没有 data」。
3. **缓存只有雏形**：没有淘汰（LRU / CLOCK）和 pin（缓存中的节点不可删除）；`minCounts_move2cache`、`linkNode_hitCounts` 还没有接入；`_can_putInto_cache` 线性扫描槽表、取第一个空槽，不区分「新放入」和「已被别的来源放入」，`_put2cache` 的 `true` 也可能只是「目标已在缓存」，而 `false` 既可能是「本来源已缓存」也可能是失败，返回语义需要拆分。节点释放时也只按 `self_in_cache` + 槽内指针校验来清槽，标志和槽不一致时可能留下悬垂槽。
4. **跨分支引用未实现**：`externalRelatedLink_nodePathP` 只有字段，没有跳转、没有路径校验——结构变化后旧路径可能静默指向另一个节点，需要路径 hash/版本号。
5. **子节点表示内存放大**：每节点固定 256 个指针 = 2 KB，稀疏树浪费严重；计划 bitmap + 紧凑数组，或参考 ART 的 Node4/16/48/256。
6. **`tempNodeP_4easyFind` 隐式状态**：`_node_isExist` 用成员变量回传结果、`_find_node` 再读取并清空；失败或提前返回时可能留下旧值，而且 `_find_node` 里还有一段实际上很难走到的重复遍历。应改为直接返回槽指针，不经过成员变量。
7. **线程安全未验证**：`privateMutex` 已接入 `createChildren`、`put_data`、`get_data`、`_put2cache` 和析构；`_find_node` / `_node_isExist` / `_mount_node` / `_can_putInto_cache` / `_free_nodeTree` 依赖调用方持锁；`free_node` 没有锁约定，锁粒度（整树锁 / 分段锁）也还没定。
8. **持久化还做不到「直接 dump」**：`childFP` 是裸指针，跨进程/落盘前必须改成 arena + 偏移，或序列化时转节点 id。
9. **没有正式测试与 CI**：`testMain.cpp` 已覆盖当前 public API 的 smoke test，但没有单元测试框架、CMake、GitHub Actions。
10. **递归释放的深度 = key 长度**：作者已知并接受深 key 可能爆栈，后续可改显式栈。
11. **命名遗留**：`size0f_HVcache`、`length_externaRelatedlLink_nodePath`、`self_cacheIndex` 等拼写/风格待统一。
12. **异常安全**：`_mount_node` / `_create_childrenArray` 在 `new` 抛异常时可能留下部分节点/空路径；目前没有回滚。

### 设计要点

- 256 路分叉 = 一次消费一个字节，树深度等于 key 的字节数
- 跨分支引用保存目标节点的**字节路径**，而不是物理指针，避免悬垂指针
- 热路径命中次数超过阈值后，把目标节点的物理指针放进树内缓存槽，实现 O(1) 跳转
- 缓存是 **non-owning** 的：节点被放进缓存后不能直接删除；淘汰 / pin 协议还没实现

### 线程与锁约定

- `privateMutex` 保护根指针、树结构和 HVcache 槽表。
- 已加锁的 public 入口：`createChildren`、`put_data`、`get_data`、`~Tree256`；`_put2cache` 内部也加锁。
- 要求调用方持锁的 private helper：`_find_node`、`_node_isExist`、`_mount_node`、`_can_putInto_cache`、`_free_nodeTree`（析构函数在持锁状态下递归调用 `_free_nodeTree`，递归本身不再加锁）。
- `free_node` 尚未实现，也没有锁约定；`put_data` / `get_data` 每次调用只持一次锁，不调用已加锁的 public `createChildren`。

### 贡献

当前阶段最需要的是**设计讨论**而不是代码量。欢迎开 issue 讨论节点布局、缓存策略、持久化方案；提 PR 前请先说明要解决的问题。

### License

[MIT](LICENSE)，Copyright (c) 2026 Tyupoi-Chronoce。完整许可证文本见 [LICENSE](LICENSE)。

### 致谢

本项目起源于一次关于「路径即坐标」的讨论——从 `0xCC` 乱码开始，到跨分支路径重定向，再到把热路径缓存想象成神经突触。感谢所有后续愿意一起把它做对的人。

---

## English

### Overview

The long-term design is described in `introduction.md` (kept local, not published; see `.gitignore`). This repository currently contains a structural prototype and a small API smoke test.

> **Status: pre-alpha, structural prototype.**
> The tree can create child-pointer arrays, read/write `dataP` along byte paths, and recursively free a whole tree. The cache is only a shell: a slot table plus the ability to put a target node into a free slot.
> **Not implemented yet:** cross-branch jumps, cache eviction/pinning, persistence, a unit-test framework, or a build system. `free_node` is still a placeholder.
> The API will change significantly. **Do not use this in production.**

### Repository layout

| File | Description |
|---|---|
| `classBasic.cpp` | All implementation. Currently a "header-style .cpp" (no `main`, no include guard); it will be split into `tree256.hpp` + `tree256.cpp` later |
| `testMain.cpp` | API smoke test (root/empty path, put/get, prefixes and embedded 0, overwrite/invalid arguments, createChildren/free_node regression); see "Build and run" |
| `introduction.md` | Design document, kept locally and not committed |
| `LICENSE` | MIT license |

### What works today

- `createChildren(path, len)` — allocate a 256-slot child-pointer array for the node at the path (idempotent; safely returns false when the path does not exist)
- `put_data(path, len, dataP)` — ensure the node exists along the path and write `dataP`; writing the same path again only overwrites the pointer and does not free the old data; `dataP` must be non-null and the tree does not own it
- `get_data(path, len)` — read `dataP` by path; returns `nullptr` when the path is missing or the node has no data; `length == 0` reads the root
- `_mount_node(path, len)` — internal; a single pass ensures every node along the path exists and returns the target node's slot `treeNode**`; does not write data; the caller must hold the lock
- `_find_node(path, len)` / `_node_isExist(path, len)` — internal lookup; `_find_node` returns the slot in the parent that points to the target node (the root returns `&rootNodeP`)
- `_free_nodeTree(&slot)` — recursively free a subtree: the child-pointer array, the external path, and the node itself are freed; `dataP` is not freed and leaves are not leaked (measured on a 3-level tree with an external path: new/delete count 10/10, balanced)
- `HVcache_nodeP` / `HVcache_aliveState` — a non-owning pointer table with `size + 1` slots; slot 0 is a sentinel; `_can_putInto_cache` / `_put2cache` can put an external-link target into a free slot
- `privateMutex` — protects the tree and cache; `createChildren`, `put_data`, `get_data`, `_put2cache`, and the destructor lock internally; other private helpers require the caller to hold the lock (see "Threading and locking")
- `free_node(path, len)` — public placeholder that always returns false; not implemented yet (see TODO)

### Build and run

There is no build system yet. `classBasic.cpp` is currently a "header-style" implementation and `testMain.cpp` includes it directly.

```bash
# Compile the implementation only (syntax check)
g++ -std=c++20 -Wall -Wextra -Wpedantic -c classBasic.cpp -o classBasic.o

# Build and run the API smoke test
g++ -std=c++20 -Wall -Wextra -Wpedantic testMain.cpp -o ckt_test
./ckt_test
```

Expected output:

```text
[test] put_data / get_data / createChildren / free_node OK
[test] TODO: external path 跳转、HVcache 准入淘汰、free_node 仍未实现
```

The current version builds with zero warnings under GCC 14.2.0 / MinGW-w64, and the test exits with code 0.

### TODO / known issues

Ordered roughly by priority; contributions are welcome:

1. **API not finalized**: the public API currently has `createChildren`, `put_data`, `get_data`, and a placeholder `free_node`. There is no `erase` / subtree deletion yet, and `free_node` semantics are undecided (single node vs. whole subtree, whether data is released, how external links and cache entries are invalidated).
2. **Key and data contract**: `put_data` / `get_data` already use explicit lengths and support embedded 0, but the external-path format still needs a length or terminator convention, and `dataP` ownership (currently: the tree never deletes it; the caller owns it) needs to be finalized. `get_data` cannot currently distinguish "path missing" from "node exists but has no data".
3. **Cache is only a shell**: no eviction (LRU / CLOCK) and no pinning (a cached node must not be deleted); `minCounts_move2cache` and `linkNode_hitCounts` are not wired in; `_can_putInto_cache` linearly scans the slot table and takes the first free slot, without distinguishing "newly inserted" from "already inserted by another source"; `_put2cache` returning `true` may only mean "the target is already cached", while `false` may mean either "this source is already cached" or a failure — the return semantics need to be split. Node destruction currently clears a slot only by checking `self_in_cache` plus the slot pointer; if the flag and the slot disagree, a dangling slot can remain.
4. **Cross-branch references are not implemented**: `externalRelatedLink_nodePathP` is only a field — there is no jump and no path validation. After the structure changes, an old path can silently point to a different node; path hashing or a global revision is needed.
5. **Child representation wastes memory**: a fixed 256 pointers per node = 2 KB, which is very wasteful for sparse trees. A planned fix is a bitmap + compact array, or ART-style Node4/16/48/256.
6. **`tempNodeP_4easyFind` implicit state**: `_node_isExist` writes the result through a member variable and `_find_node` reads and clears it; a failure or early return can leave a stale value, and `_find_node` still contains a duplicate traversal that is effectively unreachable. Return the slot pointer directly instead of going through a member variable.
7. **Thread safety is unverified**: `privateMutex` is wired into `createChildren`, `put_data`, `get_data`, `_put2cache`, and the destructor; `_find_node` / `_node_isExist` / `_mount_node` / `_can_putInto_cache` / `_free_nodeTree` depend on the caller holding the lock; `free_node` has no locking contract, and lock granularity (whole-tree lock vs. segmented locks) is undecided.
8. **Persistence cannot "just dump" yet**: `childFP` is a raw pointer; before going cross-process or to disk it must become an arena + offsets, or be serialized as node IDs.
9. **No formal tests or CI**: `testMain.cpp` covers the current public API with a smoke test, but there is no unit-test framework, CMake, or GitHub Actions.
10. **Recursive free depth = key length**: the author is aware of and accepts that very deep keys may overflow the stack; this can later be converted to an explicit stack.
11. **Naming legacy**: `size0f_HVcache`, `length_externaRelatedlLink_nodePath`, `self_cacheIndex`, and similar spelling/style issues need to be unified.
12. **Exception safety**: `_mount_node` / `_create_childrenArray` can leave partial nodes or an empty path behind if `new` throws; there is no rollback yet.

### Design notes

- 256-way fanout = consume one byte per level; tree depth equals the key's byte length
- A cross-branch reference stores the target's **byte path**, not a physical pointer, avoiding dangling pointers
- Once a hot-path hit count passes a threshold, the target's physical pointer is placed in an in-tree cache slot to make the jump O(1)
- The cache is **non-owning**: a node placed in the cache must not be deleted directly; the eviction / pinning protocol is not implemented yet

### Threading and locking

- `privateMutex` protects the root pointer, tree structure, and HVcache slot table.
- Public entry points that lock internally: `createChildren`, `put_data`, `get_data`, `~Tree256`; `_put2cache` also locks internally.
- Private helpers that require the caller to hold the lock: `_find_node`, `_node_isExist`, `_mount_node`, `_can_putInto_cache`, `_free_nodeTree` (the destructor holds the lock while recursing into `_free_nodeTree`; the recursion itself does not lock again).
- `free_node` is not implemented and has no locking contract yet; `put_data` / `get_data` take the lock once per call and do not call the already-locking public `createChildren`.

### Contributing

At this stage, **design discussion** matters more than code volume. Issues about node layout, cache policy, and persistence are welcome. Before opening a PR, please explain the problem it solves.

### License

[MIT](LICENSE), Copyright (c) 2026 Tyupoi-Chronoce. See [LICENSE](LICENSE) for the full text.

### Acknowledgements

The project started as a discussion about "path as coordinate" — from `0xCC` mojibake, to cross-branch path redirection, to imagining the hot-path cache as a synapse. Thanks to everyone who is willing to help get it right.
