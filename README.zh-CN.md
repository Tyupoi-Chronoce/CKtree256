# CKtree256

一个实验性的 **256 叉基数树**（byte-radix tree）实现。长期目标是给树内/跨分支的节点引用找到一种「可持久化、跨进程、无悬垂指针」的表达方式：用**逻辑字节路径**代替裸指针，并用**热路径缓存**把跨分支跳转摊平成 O(1)。

**语言：** [English](README.md) · 简体中文

---

## 项目简介

长期设想见设计文档 `introduction.md`（暂未公开，见 `.gitignore`）。本仓库目前包含：

- `classBasic.cpp`：单文件开发版实现，冒烟测试直接包含它；
- `smoke.cpp`：DSH 维护的 public API 冒烟测试；
- `testMain.cpp`：作者自用的实测入口，目前保留为可编译的最小占位；
- `_4lib_work/CKtree256.hpp` + `.cpp`：当前的**库化拆分**，由 `classBasic.cpp` 生成（2026-11-16）；
- `lib/libCKtree256.a` + `lib/CKtree256.hpp`：对应的 MinGW-w64 静态库和头文件；
- `docs/C_API_design.md`：未来 C ABI 的设计草案（未实现）；
- `docs/threading_design_notes.md`：计划中的后台缓存淘汰线程的注意事项。

> **状态：pre-alpha，结构性原型。**
> 已能按字节路径创建子指针数组、读写 / 清空 `dataP`、按逻辑路径把两个已存在节点链接起来、按源路径用 `get_linkNode(path, len)` 解析链接、递归释放子树。HVcache 有「目标节点被跳转次数阈值 + 空槽准入」、每槽 generation 校验，还有一个私有的 `_cache_eviction`（sweep / 惩罚 / 最少命中次数兜底）；淘汰辅助函数还没有接到准入流程或后台线程，也没有 pin。
> **还没有**缓存淘汰的接线和后台线程、pin、公开且稳定的 node 句柄/身份、结构变化后的路径校验、持久化、正式测试框架和构建系统。`get_linkNode` 现在接收路径并返回 `treeNode*`；这个指针是内部节点地址，不是稳定句柄，只在节点存活期间有效。
> 接口会大改，**不要用于生产**。

## 目录

| 文件 / 目录 | 说明 |
|---|---|
| `classBasic.cpp` | 单文件开发版实现（头文件式 .cpp，无 `main`、无 include guard）；`smoke.cpp` 直接包含它 |
| `smoke.cpp` | DSH 维护的 public API 冒烟测试（包含 `classBasic.cpp`，不链接静态库） |
| `testMain.cpp` | 作者自用的实测入口；DSH 不再编辑它 |
| `_4lib_work/CKtree256.hpp` | 库化拆分的 public header（当前 API，仅类声明） |
| `_4lib_work/CKtree256.cpp` | 库化拆分的类外实现（当前 API） |
| `lib/libCKtree256.a` | 由当前拆分构建的静态库（C++ ABI，GCC 14.2.0 / MinGW-w64） |
| `lib/CKtree256.hpp` | 随库分发的头文件（`_4lib_work/CKtree256.hpp` 的副本） |
| `docs/C_API_design.md` | C ABI 设计草案，未实现 |
| `docs/threading_design_notes.md` | 计划中的缓存淘汰线程：生命周期、锁顺序和指针存活期注意事项 |
| `introduction.md` | 最初的设计讨论，本地保留、未入库 |
| `README.md` | 英文版主 README |
| `README.zh-CN.md` | 中文版 README（本文件） |
| `LICENSE` | MIT 许可证 |

> 维护关系：`classBasic.cpp` 是开发源；`_4lib_work` 是它的机械拆分，public API 与源码一致；`lib` 由 `_4lib_work` 构建。修改 `classBasic.cpp` 后，按下面的命令重新生成拆分并重建静态库；`smoke.cpp` 直接包含源码，不链接静态库。源码和拆分目前在 GCC 14.2.0 / MinGW-w64 上都零警告编译；拆分和 `lib/` 已于 2026-11-16 刷新。

## 目前已经能做什么

- `createChildren(path, len)` —— 给已存在节点分配 256 槽子指针数组（幂等；路径不存在时安全返回 false）
- `put_data(path, len, dataP)` —— 沿路径确保节点存在并写入 `dataP`；同一路径重复写入只覆盖指针，不释放旧 data；`dataP` 必须非空，树不拥有 data
- `get_data(path, len)` —— 按路径读取 `dataP`；路径不存在或节点没有 data 时返回 `nullptr`；`length == 0` 读根节点
- `erase_data(path, len)` —— 清空已存在节点的 `dataP`，不删除节点；路径不存在返回 false
- `link_node(dstPath, dstLen, targetPath, targetLen)` —— 按逻辑路径把一个已存在目标节点链接到已存在目的节点；重复调用替换旧路径；两端都必须已存在。目标路径用会抛异常的 `new` 复制，分配失败会传播 `std::bad_alloc`；和 `createChildren` / `put_data` 一样，这个 public `bool` 接口可能抛异常。
- `get_linkNode(path, len)` —— 解析 `path` 所指向节点上保存的链接；`length == 0` 表示把根节点作为源节点。未命中时走保存的目标路径；**目标节点**的 `hitCounts_4beJumped` 超过阈值（`minCounts_move2cache = 0x0f`）后把该目标放进 HVcache。每次成功解析递增的是目标节点的计数（到 `UINT32_MAX` 饱和），不是源节点的计数。缓存命中时先校验槽 index、generation 和目标自身的缓存标志，再返回缓存中的目标节点 `treeNode*`（不是槽表里的地址）；槽失效时丢弃记录并回到路径查找。源节点不存在、目标不存在或没有链接都返回 `nullptr`。返回的指针只在目标节点存活期间有效。
- `free_nodeTree(path, len)` —— 递归释放子树、每个节点复制保存的 external path 和它自己的缓存槽；**不释放 `dataP`**，不允许释放根节点；路径不存在返回 false
- `_mount_node` / `_find_node` / `_create_childrenArray` / `_free_nodeTree` / `_can_putInto_cache` / `_put2cache` / `_get_linkNode` / `_cache_eviction` / `_remove_self_from_cache` / `_forget_link_cache` —— internal 辅助函数；调用方必须持锁；路径版 `_put2cache(path, len)` 只负责解析源节点，然后委托给 `_put2cache(treeNode**, ...)`；`_cache_eviction` 目前没有调用方
- `HVcache_nodeP` / `HVcache_aliveState` / `HVcache_generation` —— `size + 1` 个槽的非拥有指针表，槽 0 为哨兵；`_can_putInto_cache` 先线性扫描已缓存目标，再取第一个空槽，并报告是否为新放入。`_cache_eviction` 可以 sweep 冷槽、施加淘汰惩罚，槽满时淘汰命中次数最少的节点，但目前还没有调用方
- `privateMutex` —— 保护树结构与缓存；public 入口每次调用持一次锁，private helper 要求调用方持锁

## 库化拆分

仓库里保留了一份与当前源码同步的库化拆分：

```text
_4lib_work/CKtree256.hpp      # public header，只放类声明
_4lib_work/CKtree256.cpp      # 类外实现
lib/CKtree256.hpp             # 随库分发的头文件
lib/libCKtree256.a            # 由拆分构建的静态库
```

修改 `classBasic.cpp` 后重新构建：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

使用方链接：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilib app.cpp lib/libCKtree256.a -o app
```

拆分已于 2026-11-16 从当前 `classBasic.cpp` 重新生成，暴露当前类 API（`createChildren`、`put_data`、`get_data`、`erase_data`、`link_node(path, path)`、`get_linkNode(path, len)`、`free_nodeTree`、析构）。用上面的命令编译的临时 consumer 输出 `consumer OK sizeof(Tree256)=64 link/get_linkNode OK`，覆盖 root/key 的 `put_data`、`link_node`、按路径的 `get_linkNode`（含 20 次重复跳转和重新链接到 root）以及 `free_nodeTree`；`get_data`、`erase_data`、`createChildren` 由 `smoke.cpp` 覆盖。

当前只提供 **C++ 类接口**，没有 C 风格 / `extern "C"` 头文件。`Tree256` 依赖 `std::mutex`，`get_linkNode` 返回的是裸 `treeNode*`，不是稳定句柄。未来要给 C、Python 或其它语言用，建议单独加一层 opaque handle + `extern "C"` wrapper，并定义返回节点的存活期规则；设计草案见 `docs/C_API_design.md`。

## 编译与运行

单文件冒烟测试（直接包含 `classBasic.cpp`）：

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic smoke.cpp -o smoke
./smoke
```

预期输出：

```text
[smoke] put_data / get_data / erase_data / createChildren / link_node / get_linkNode / free_nodeTree OK
[smoke] target-side hitCounts_4beJumped admission, cache hit, relink and cached-target free OK
```

当前版本在 GCC 14.2.0 / MinGW-w64 上编译零警告，冒烟测试退出码 0。`testMain.cpp` 是作者自用文件；当前占位也能编译，但不属于冒烟测试覆盖范围。

## TODO / 已知问题

按优先级排列，欢迎认领：

1. **接口未定型**：public 有 `createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode` 和 `free_nodeTree`。link 路径现在可以端到端使用，但还没有公开且稳定的 node 句柄/身份：`get_linkNode` 返回的裸 `treeNode*` 只在目标存活期间有效。`free_nodeTree` 已能释放结构和缓存槽，但 data 删除回调、单节点 vs 子树语义还要定。
2. **key 与数据契约**：显式 length 已支持嵌入 0；外部路径格式仍需要长度或终结符约定，`dataP` 所有权（当前约定树不 delete，调用方负责）也要最终写死。`get_data` 目前不能区分「路径不存在」和「节点存在但没有 data」。
3. **缓存尚未完成**：目标侧阈值和 generation 校验已接到 `get_linkNode`。私有的 `_cache_eviction` 已实现 sweep、淘汰惩罚和最少命中次数兜底，但还没有调用方把它接进准入流程，也没有后台线程或 pin（缓存中的节点不可删除）。`_can_putInto_cache` 线性扫描槽表、取第一个空槽；路径版 `_put2cache` 只解析源节点并委托给指针重载，准入逻辑只保留一份；缓存命中返回缓存中的目标节点指针，不是稳定句柄。
4. **跨分支引用只完成了一半**：现在保存的是逻辑路径副本，但还没有路径校验；结构变化后旧路径可能静默指向另一个节点，需要路径 hash/版本号。并且缺少公开 node 身份/句柄，`get_linkNode` 返回的 `treeNode*` 在 `free_nodeTree` 后可能悬垂。
5. **子节点表示内存放大**：每节点固定 256 个指针 = 2 KB，稀疏树浪费严重；计划 bitmap + 紧凑数组，或参考 ART 的 Node4/16/48/256。
6. **`tempNodeP_4easyFind` 隐式状态**：`_node_isExist` 用成员变量回传结果、`_find_node` 再读取并清空；失败或提前返回时可能留下旧值，而且 `_find_node` 里还有一段实际上很难走到的重复遍历。应改为直接返回槽指针，不经过成员变量。
7. **线程安全未验证**：`privateMutex` 已接入 `createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode`、`free_nodeTree` 和析构；private helper 要求调用方持锁。没有并发测试，锁粒度也还没定。后台淘汰线程有计划但未实现；生命周期和锁顺序约束见 `docs/threading_design_notes.md`。
8. **持久化还做不到「直接 dump」**：`childFP` 是裸指针，跨进程/落盘前必须改成 arena + 偏移，或序列化时转节点 id。
9. **没有正式测试与 CI**：`smoke.cpp` 覆盖当前 public API，包括 `link_node` / `get_linkNode`、重复跳转、重新链接和缓存中目标的释放；`testMain.cpp` 是作者自用文件。但没有单元测试框架、CMake、GitHub Actions，`_cache_eviction` 因为没有调用方也没有直接测试。
10. **递归释放的深度 = key 长度**：作者已知并接受深 key 可能爆栈，后续可改显式栈。
11. **命名遗留**：`size0f_HVcache`、`length_externaRelatedlLink_nodePath`、`self_cacheIndex` 等拼写/风格待统一。
12. **异常安全**：`_mount_node` / `_create_childrenArray` 和 `_link_node` 现在都用抛异常的 `new`，所以 public `bool` 接口可能抛 `std::bad_alloc`（头文件注释已写明）；需要 no-throw 契约的调用方必须自己 catch。其它失败路径仍返回 `false`。
13. **库产物会漂移**：`_4lib_work` 是 `classBasic.cpp` 的手工拆分，`lib` 是针对 GCC 14.2.0 / MinGW-w64 预编译的。源码变化后要重跑构建命令；换 ABI 时应重新编译，不要直接复用这个 archive。

## 设计要点

- 256 路分叉 = 一次消费一个字节，树深度等于 key 的字节数
- 跨分支引用保存目标节点的**字节路径**，而不是物理指针，避免悬垂指针
- 目标节点的 `hitCounts_4beJumped` 超过阈值后，把它的物理指针放进树内缓存槽，实现 O(1) 跳转
- 缓存是 **non-owning** 的：节点被放进缓存后不能直接删除；`_cache_eviction` 已存在但未接线，pin 协议也还没实现
- 槽位带 generation；缓存命中必须同时匹配源节点记录的 generation 和目标节点的 self-cache 标志，否则回退到路径查找
- `get_linkNode` 返回目标节点本身，不是槽地址；该指针只在目标节点存活期间有效，也不是跨进程稳定句柄
- 库接口保持 C++ 类风格；C ABI 需要额外的 opaque handle 包装层，暂不提供

## 线程与锁约定

- `privateMutex` 保护根指针、树结构和 HVcache 槽表。
- 已加锁的 public 入口：`createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode`、`free_nodeTree`、`~Tree256`。`link_node` 持锁期间可能抛 `std::bad_alloc`，由 RAII 在栈展开时释放锁。`get_linkNode` 解锁后返回裸节点指针，存活期由调用方负责。
- 要求调用方持锁的 private helper：`_find_node`、`_node_isExist`、`_mount_node`、`_create_childrenArray`、`_can_putInto_cache`、`_remove_self_from_cache`、`_forget_link_cache`、`_put2cache`、`_get_linkNode`、`_cache_eviction`、`_find_from_cache`、`_free_nodeTree`（析构函数在持锁状态下递归调用 `_free_nodeTree`，递归本身不再加锁）。
- `free_nodeTree` 不允许释放根节点；public 入口每次调用只持一次锁，不调用已经加锁的 public 函数。
- 目前没有后台线程。若为 `_cache_eviction` 增加后台线程：构造完成后才启动，析构取 `privateMutex` 之前先停止并 join，worker 内不要调用已经加锁的 public 方法；完整清单见 `docs/threading_design_notes.md`。

## 贡献

当前阶段最需要的是**设计讨论**而不是代码量。欢迎开 issue 讨论节点布局、缓存策略、持久化方案；提 PR 前请先说明要解决的问题。

## License

[MIT](LICENSE)，Copyright (c) 2026 Tyupoi-Chronoce。完整许可证文本见 [LICENSE](LICENSE)。

## 致谢

本项目起源于一次关于「路径即坐标」的讨论——从 `0xCC` 乱码开始，到跨分支路径重定向，再到把热路径缓存想象成神经突触。感谢所有后续愿意一起把它做对的人。

---

**语言：** [English](README.md) · 简体中文
