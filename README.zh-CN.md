# CKtree256

一个实验性的 **256 叉基数树**（byte-radix tree）实现。长期目标是给树内/跨分支的节点引用找到一种「可持久化、跨进程、无悬垂指针」的表达方式：用**逻辑字节路径**代替裸指针，并用**热路径缓存**把跨分支跳转摊平成 O(1)。

**语言：** [English](README.md) · 简体中文

---

## 项目简介

长期设想见设计文档 `introduction.md`（暂未公开，见 `.gitignore`）。本仓库目前包含：

- `classBasic.cpp`：单文件开发版实现，定义类模板 `Tree256<T>`；冒烟测试直接包含它；
- `smoke.cpp`：DSH 维护的 public API 冒烟测试；实例化 `Tree256<int>`；
- `testMain.cpp`：作者自用的实测入口，目前保留为可编译的最小占位；
- `_4lib_work/CKtree256.hpp` + `.cpp`：2026-10-28 的**库化快照**；
- `lib/libCKtree256.a` + `lib/CKtree256.hpp`：由该快照构建的静态库和头文件；
- `docs/C_API_design.md`：未来 C ABI 的设计草案（未实现）；
- `docs/VSCode_Git_workflow.md`：VSCode 源代码管理 / Git 操作注意事项（中文）。

> **状态：pre-alpha，结构性原型。**
> 已能按字节路径创建子指针数组、读写 / 清空 `T*` 数据指针、按逻辑路径把两个已存在节点链接起来、在拿到节点句柄时解析链接、递归释放子树。HVcache 有「阈值 + 空槽准入」和每槽 generation 校验，但没有淘汰和 pin。
> **还没有**可公开使用的跨分支节点句柄、缓存淘汰 / pin、结构变化后的路径校验、持久化、正式测试框架和构建系统。`get_linkNode` 仍接收 `treeNode<T>**`，所以外部调用方还无法端到端使用 link/cache 路径。
> 接口会大改，**不要用于生产**。

## 目录

| 文件 / 目录 | 说明 |
|---|---|
| `classBasic.cpp` | 单文件开发版实现（头文件式 .cpp，无 `main`、无 include guard）；定义类模板 `Tree256<T>`；`smoke.cpp` 直接包含它 |
| `smoke.cpp` | DSH 维护的 public API 冒烟测试；实例化 `Tree256<int>`（包含 `classBasic.cpp`，不链接静态库） |
| `testMain.cpp` | 作者自用的实测入口；DSH 不再编辑它 |
| `_4lib_work/CKtree256.hpp` | 库化快照的 public header（旧 API） |
| `_4lib_work/CKtree256.cpp` | 库化快照的实现（旧 API） |
| `lib/libCKtree256.a` | 由 2026-10-28 快照构建的静态库（C++ ABI，GCC 14.2.0 / MinGW-w64） |
| `lib/CKtree256.hpp` | 随库分发的快照头文件副本 |
| `docs/C_API_design.md` | C ABI 设计草案，未实现 |
| `docs/VSCode_Git_workflow.md` | VSCode 源代码管理 / Git 操作注意事项（中文） |
| `introduction.md` | 最初的设计讨论，本地保留、未入库 |
| `README.md` | 英文版主 README |
| `README.zh-CN.md` | 中文版 README（本文件） |
| `LICENSE` | MIT 许可证 |

> 维护关系：`classBasic.cpp` 是开发源；`_4lib_work` 和 `lib` 是 **2026-10-28 的旧快照**，还没有 path 版 `link_node` 和 `free_nodeTree`。发布前要么重新同步并构建，要么不要把它们当作当前 API。单文件源目前能在 GCC 14.2.0 / MinGW-w64 上零警告编译，`smoke.cpp` 实例化 `Tree256<int>`。

## 目前已经能做什么

`Tree256<T>` 是类模板。`T` 是调用方的数据类型，每个节点保存一个 `T*`（`dataP`），树不拥有这份数据。`smoke.cpp` 实例化 `Tree256<int>`。因为 `classBasic.cpp` 仍被直接包含，模板定义对该翻译单元可见。

- `createChildren(path, len)` —— 给已存在节点分配 256 槽子指针数组（幂等；路径不存在时安全返回 false）
- `put_data(path, len, T* dataP)` —— 沿路径确保节点存在并写入 `dataP`；同一路径重复写入只覆盖指针，不释放旧 data；`dataP` 必须非空，树不拥有 data
- `get_data(path, len)` —— 按路径读取保存的 `T*`；路径不存在或节点没有 data 时返回 `nullptr`；`length == 0` 读根节点
- `erase_data(path, len)` —— 清空已存在节点的 `dataP`，不删除节点；路径不存在返回 false
- `link_node(dstPath, dstLen, targetPath, targetLen)` —— 按逻辑路径把一个已存在目标节点链接到已存在目的节点；重复调用替换旧路径；两端都必须已存在
- `get_linkNode(nodeP)` —— 从 `treeNode<T>**` 句柄解析链接；未命中时走保存的路径；源节点命中次数超过阈值（`minCounts_move2cache = 0x0f`）后尝试把目标节点放进 HVcache。缓存命中时先校验槽 index、generation 和目标自身的缓存标志，再返回缓存槽内的指针，不是目标节点原来的父槽；槽失效时丢弃记录并回到路径查找
- `free_nodeTree(path, len)` —— 递归释放子树、每个节点复制保存的 external path 和它自己的缓存槽；**不释放 `dataP`**，不允许释放根节点；路径不存在返回 false
- `_mount_node` / `_find_node` / `_create_childrenArray` / `_free_nodeTree` / `_can_putInto_cache` / `_put2cache` / `_remove_self_from_cache` / `_forget_link_cache` —— internal 辅助函数；调用方必须持锁
- `HVcache_nodeP` / `HVcache_aliveState` / `HVcache_generation` —— `size + 1` 个槽的非拥有指针表，槽 0 为哨兵；`_can_putInto_cache` 先线性扫描已缓存目标，再取第一个空槽，并报告是否为新放入
- `privateMutex` —— 保护树结构与缓存；public 入口每次调用持一次锁，private helper 要求调用方持锁

## 库化快照

仓库里保留了一份较早的库化拆分：

```text
_4lib_work/CKtree256.hpp      # 快照 public header
_4lib_work/CKtree256.cpp      # 快照实现
lib/CKtree256.hpp             # 头文件副本
lib/libCKtree256.a            # 由快照构建的静态库
```

从快照重新构建：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

使用方链接：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilib app.cpp lib/libCKtree256.a -o app
```

这份快照仍暴露旧的 `free_node` / `link_node(treeNode**, ...)` API，**不是**当前 `classBasic.cpp` 的构建产物。当前只提供 **C++ 类接口**，没有 C 风格 / `extern "C"` 头文件。当前 `Tree256<T>` 依赖 `std::mutex`，`get_linkNode` 还暴露 `treeNode<T>**` 句柄。未来要给 C、Python 或其它语言用，建议单独加一层 opaque handle + `extern "C"` wrapper；设计草案见 `docs/C_API_design.md`。

## 编译与运行

单文件冒烟测试（直接包含 `classBasic.cpp`）：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic smoke.cpp -o smoke
./smoke
```

预期输出：

```text
[smoke] put_data / get_data / erase_data / createChildren / link_node / free_nodeTree OK
[smoke] TODO: successful link_node -> get_linkNode needs a public node handle
```

当前版本在 GCC 14.2.0 / MinGW-w64 上编译零警告，冒烟测试（`Tree256<int>`）退出码 0。`testMain.cpp` 是作者自用文件；当前占位也能编译，但不属于冒烟测试覆盖范围。

## TODO / 已知问题

按优先级排列，欢迎认领：

1. **接口未定型**：`Tree256<T>` 必须显式指定数据类型（没有默认模板参数），public 有 `createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode` 和 `free_nodeTree`。还没有公开的 node 句柄入口，所以外部调用方无法端到端使用 `get_linkNode` / 缓存路径。`free_nodeTree` 已能释放结构和缓存槽，但 data 删除回调、单节点 vs 子树语义还要定。
2. **key 与数据契约**：显式 length 已支持嵌入 0；外部路径格式仍需要长度或终结符约定，`dataP` 所有权（当前约定树不 delete，调用方负责）也要最终写死。`get_data` 目前不能区分「路径不存在」和「节点存在但没有 data」。
3. **缓存尚未完成**：阈值和 generation 校验已接到 `get_linkNode`，但还没有淘汰（LRU / CLOCK）和 pin（缓存中的节点不可删除）。`_can_putInto_cache` 线性扫描槽表、取第一个空槽；两个 `_put2cache` 重载逻辑重复，返回语义也需要拆分；缓存命中返回的是槽表内指针，不是稳定节点句柄。
4. **跨分支引用只完成了一半**：现在保存的是逻辑路径副本，但还没有路径校验；结构变化后旧路径可能静默指向另一个节点，需要路径 hash/版本号。并且缺少公开 node 身份/句柄。
5. **子节点表示内存放大**：每节点固定 256 个指针 = 2 KB，稀疏树浪费严重；计划 bitmap + 紧凑数组，或参考 ART 的 Node4/16/48/256。
6. **`tempNodeP_4easyFind` 隐式状态**：`_node_isExist` 用成员变量回传结果、`_find_node` 再读取并清空；失败或提前返回时可能留下旧值，而且 `_find_node` 里还有一段实际上很难走到的重复遍历。应改为直接返回槽指针，不经过成员变量。
7. **线程安全未验证**：`privateMutex` 已接入 `createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode`、`free_nodeTree` 和析构；private helper 要求调用方持锁。没有并发测试，锁粒度也还没定。
8. **持久化还做不到「直接 dump」**：`childFP` 是裸指针，跨进程/落盘前必须改成 arena + 偏移，或序列化时转节点 id。
9. **没有正式测试与 CI**：`smoke.cpp` 用 `Tree256<int>` 覆盖当前 public API，`testMain.cpp` 是作者自用文件；但没有单元测试框架、CMake、GitHub Actions。
10. **递归释放的深度 = key 长度**：作者已知并接受深 key 可能爆栈，后续可改显式栈。
11. **命名遗留**：`size0f_HVcache`、`length_externaRelatedlLink_nodePath`、`self_cacheIndex` 等拼写/风格待统一。
12. **异常安全**：`_mount_node` / `_create_childrenArray` 用抛异常的 `new`，`_link_node` 用 nothrow `new`；对外的 `bool` API 异常契约不一致。
13. **快照会漂移**：`_4lib_work` 和 `lib` 比 `classBasic.cpp` 旧。发布前要么同步并重新构建，要么移除这些产物，避免使用者编译到旧 API。

## 设计要点

- 256 路分叉 = 一次消费一个字节，树深度等于 key 的字节数
- 跨分支引用保存目标节点的**字节路径**，而不是物理指针，避免悬垂指针
- 热路径命中次数超过阈值后，把目标节点的物理指针放进树内缓存槽，实现 O(1) 跳转
- 缓存是 **non-owning** 的：节点被放进缓存后不能直接删除；淘汰 / pin 协议还没实现
- 槽位带 generation；缓存命中必须同时匹配源节点记录的 generation 和目标节点的 self-cache 标志，否则回退到路径查找
- 库接口保持 C++ 类风格；C ABI 需要额外的 opaque handle 包装层，暂不提供

## 线程与锁约定

- `privateMutex` 保护根指针、树结构和 HVcache 槽表。
- 已加锁的 public 入口：`createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode`、`free_nodeTree`、`~Tree256`。
- 要求调用方持锁的 private helper：`_find_node`、`_node_isExist`、`_mount_node`、`_create_childrenArray`、`_can_putInto_cache`、`_remove_self_from_cache`、`_forget_link_cache`、`_put2cache`、`_find_from_cache`、`_free_nodeTree`（析构函数在持锁状态下递归调用 `_free_nodeTree`，递归本身不再加锁）。
- `free_nodeTree` 不允许释放根节点；public 入口每次调用只持一次锁，不调用已经加锁的 public 函数。

## 贡献

当前阶段最需要的是**设计讨论**而不是代码量。欢迎开 issue 讨论节点布局、缓存策略、持久化方案；提 PR 前请先说明要解决的问题。

## License

[MIT](LICENSE)，Copyright (c) 2026 Tyupoi-Chronoce。完整许可证文本见 [LICENSE](LICENSE)。

## 致谢

本项目起源于一次关于「路径即坐标」的讨论——从 `0xCC` 乱码开始，到跨分支路径重定向，再到把热路径缓存想象成神经突触。感谢所有后续愿意一起把它做对的人。

---

**语言：** [English](README.md) · 简体中文
