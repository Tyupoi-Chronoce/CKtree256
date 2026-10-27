# CKtree256

一个实验性的 **256 叉基数树**（byte-radix tree）实现。长期目标是给树内/跨分支的节点引用找到一种「可持久化、跨进程、无悬垂指针」的表达方式：用**逻辑字节路径**代替裸指针，并用**热路径缓存**把跨分支跳转摊平成 O(1)。

**语言：** [English](README.md) · 简体中文

---

## 项目简介

长期设想见设计文档 `introduction.md`（暂未公开，见 `.gitignore`）。本仓库目前包含：

- `classBasic.cpp`：单文件开发版实现，测试仍直接包含它；
- `_4lib_work/CKtree256.hpp` + `.cpp`：从单文件版拆分出的**库化版本**（声明 / 定义分离）；
- `lib/libCKtree256.a` + `lib/CKtree256.hpp`：已编译的静态库和公开头文件；
- `testMain.cpp`：当前 public API 的 smoke test。

> **状态：pre-alpha，结构性原型。**
> 已能按字节路径创建子指针数组、读写 / 清空 `dataP`、递归释放整棵树；HVcache 有「阈值 + 空槽准入」雏形，但没有淘汰和 pin。
> **还没有**可公开使用的跨分支节点句柄、缓存淘汰 / pin、持久化、正式测试框架和构建系统；`free_node` 仍是占位。
> 接口会大改，**不要用于生产**。

## 目录

| 文件 / 目录 | 说明 |
|---|---|
| `classBasic.cpp` | 单文件开发版实现（头文件式 .cpp，无 `main`、无 include guard）；`testMain.cpp` 直接包含它 |
| `_4lib_work/CKtree256.hpp` | 库化版本的 public header：类声明、公开 API 注释（英中双语） |
| `_4lib_work/CKtree256.cpp` | 库化版本的实现：`Tree256` 成员函数定义、内部 `treeNode` / 缓存辅助类型 |
| `lib/libCKtree256.a` | 静态库（C++ ABI，GCC 14.2.0 / MinGW-w64 构建） |
| `lib/CKtree256.hpp` | 随库分发的公开头文件副本 |
| `testMain.cpp` | public API smoke test（直接包含 `classBasic.cpp`，不链接静态库） |
| `introduction.md` | 设计文档，本地保留、未入库 |
| `README.md` | 英文版主 README |
| `README.zh-CN.md` | 中文版 README（本文件） |
| `LICENSE` | MIT 许可证 |

> 维护关系：`classBasic.cpp` 是开发源，`_4lib_work` 是它的库化快照；两边可能暂时漂移，发布前需要同步。单文件版和库化版目前都能在 GCC 14.2.0 / MinGW-w64 上零警告编译。

## 目前已经能做什么

- `createChildren(path, len)` —— 给已存在节点分配 256 槽子指针数组（幂等；路径不存在时安全返回 false）
- `put_data(path, len, dataP)` —— 沿路径确保节点存在并写入 `dataP`；同一路径重复写入只覆盖指针，不释放旧 data；`dataP` 必须非空，树不拥有 data
- `get_data(path, len)` —— 按路径读取 `dataP`；路径不存在或节点没有 data 时返回 `nullptr`；`length == 0` 读根节点
- `erase_data(path, len)` —— 清空已存在节点的 `dataP`，不删除节点；路径不存在返回 false
- `link_node(dstNodeP, path, len)` —— 把目标节点的逻辑路径复制到 `dstNodeP` 节点；重复调用替换旧路径；目前需要调用方已有 `treeNode**` 句柄
- `get_linkNode(nodeP)` —— 解析 external link：未命中时走路径；源节点命中次数超过阈值（`minCounts_move2cache = 0x0f`）后尝试把目标节点放进 HVcache，之后直接走槽表；缓存命中时返回的是缓存槽的 `treeNode**`，不是目标节点的原槽，不要依赖指针身份
- `_mount_node(path, len)` / `_find_node(path, len)` / `_create_childrenArray(slot)` / `_free_nodeTree(&slot)` —— internal 辅助函数；调用方持锁，见「线程与锁约定」
- `HVcache_nodeP` / `HVcache_aliveState` —— `size + 1` 个槽的非拥有指针表，槽 0 为哨兵；`_can_putInto_cache` 线性扫描槽表，优先复用已缓存的目标，否则取第一个空槽
- `privateMutex` —— 保护树结构与缓存；public 入口每次调用持一次锁，private helper 要求调用方持锁
- `free_node(path, len, freeData)` —— public 占位接口，目前固定返回 false，尚未实现

## 静态库

仓库已经包含构建产物：

```text
lib/CKtree256.hpp      # public header
lib/libCKtree256.a     # static library
```

从 `_4lib_work` 重新生成：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

使用方链接：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilib app.cpp lib/libCKtree256.a -o app
```

当前只提供 **C++ 类接口**，没有 C 风格 / `extern "C"` 头文件。`Tree256` 依赖 `std::mutex`，`link_node` / `get_linkNode` 还暴露了 `treeNode**` 句柄；如果以后要给 C、Python 或其它语言用，建议单独加一层 opaque handle + `extern "C"` wrapper，而不是把内部 `treeNode` 放进 C 头文件。现在先保持 C++ 静态库，避免过早冻结 ABI。

## 编译与运行

单文件 smoke test（仍直接包含 `classBasic.cpp`）：

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic testMain.cpp -o ckt_test
./ckt_test
```

预期输出：

```text
[test] put_data / get_data / erase_data / createChildren / free_node OK
[test] TODO: external link / HVcache 缺公开 node 入口，free_node 仍未实现
```

当前版本在 GCC 14.2.0 / MinGW-w64 上编译零警告，test 退出码 0。库化版本已用同样的 `-Wall -Wextra -Wpedantic` 编译，并通过一个临时 consumer probe 链接 `lib/libCKtree256.a` 实测（put/get/erase/createChildren/析构，exit 0）。

## TODO / 已知问题

按优先级排列，欢迎认领：

1. **接口未定型**：public 有 `createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode` 和占位 `free_node`；没有公开的 node 句柄入口，所以 `link_node` / `get_linkNode` 目前外部还无法真正使用。`free_node` 的语义（删除单个节点还是整棵子树、data 是否释放、外链和缓存如何失效）待定。
2. **key 与数据契约**：显式 length 已支持嵌入 0；外部路径格式仍需要长度或终结符约定，`dataP` 所有权（当前约定树不 delete，调用方负责）也要最终写死。`get_data` 目前不能区分「路径不存在」和「节点存在但没有 data」。
3. **缓存只有雏形**：阈值已接到 `get_linkNode`，但还没有淘汰（LRU / CLOCK）和 pin（缓存中的节点不可删除）；`_can_putInto_cache` 线性扫描槽表、取第一个空槽，不区分「新放入」和「已被别的来源放入」；两个 `_put2cache` 重载逻辑重复，返回语义也需要拆分。节点释放时只按 `self_in_cache` + 槽内指针校验来清槽，标志和槽不一致时可能留下悬垂槽。
4. **跨分支引用只完成了一半**：现在保存的是逻辑路径副本，但还没有路径校验；结构变化后旧路径可能静默指向另一个节点，需要路径 hash/版本号。并且缺少公开 node 句柄。
5. **子节点表示内存放大**：每节点固定 256 个指针 = 2 KB，稀疏树浪费严重；计划 bitmap + 紧凑数组，或参考 ART 的 Node4/16/48/256。
6. **`tempNodeP_4easyFind` 隐式状态**：`_node_isExist` 用成员变量回传结果、`_find_node` 再读取并清空；失败或提前返回时可能留下旧值，而且 `_find_node` 里还有一段实际上很难走到的重复遍历。应改为直接返回槽指针，不经过成员变量。
7. **线程安全未验证**：`privateMutex` 已接入 `createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode`、`_put2cache`（现在是 caller-lock，内部不再加锁）和析构；`_find_node` / `_node_isExist` / `_mount_node` / `_can_putInto_cache` / `_free_nodeTree` 依赖调用方持锁；`free_node` 没有锁约定，锁粒度也还没定。
8. **持久化还做不到「直接 dump」**：`childFP` 是裸指针，跨进程/落盘前必须改成 arena + 偏移，或序列化时转节点 id。
9. **没有正式测试与 CI**：`testMain.cpp` 覆盖当前 public API 的 smoke test，库化版本还有一个临时 consumer probe；但没有单元测试框架、CMake、GitHub Actions。
10. **递归释放的深度 = key 长度**：作者已知并接受深 key 可能爆栈，后续可改显式栈。
11. **命名遗留**：`size0f_HVcache`、`length_externaRelatedlLink_nodePath`、`self_cacheIndex` 等拼写/风格待统一。
12. **异常安全**：`_mount_node` / `_create_childrenArray` 在 `new` 抛异常时可能留下部分节点/空路径；目前没有回滚。

## 设计要点

- 256 路分叉 = 一次消费一个字节，树深度等于 key 的字节数
- 跨分支引用保存目标节点的**字节路径**，而不是物理指针，避免悬垂指针
- 热路径命中次数超过阈值后，把目标节点的物理指针放进树内缓存槽，实现 O(1) 跳转
- 缓存是 **non-owning** 的：节点被放进缓存后不能直接删除；淘汰 / pin 协议还没实现
- 库接口保持 C++ 类风格；C ABI 需要额外的 opaque handle 包装层，暂不提供

## 线程与锁约定

- `privateMutex` 保护根指针、树结构和 HVcache 槽表。
- 已加锁的 public 入口：`createChildren`、`put_data`、`get_data`、`erase_data`、`link_node`、`get_linkNode`、`~Tree256`。
- 要求调用方持锁的 private helper：`_find_node`、`_node_isExist`、`_mount_node`、`_create_childrenArray`、`_can_putInto_cache`、`_remove_from_cache`、`_put2cache`、`_find_from_cache`、`_free_nodeTree`（析构函数在持锁状态下递归调用 `_free_nodeTree`，递归本身不再加锁）。
- `free_node` 尚未实现，也没有锁约定；public 入口每次调用只持一次锁，不调用已经加锁的 public 函数。

## 贡献

当前阶段最需要的是**设计讨论**而不是代码量。欢迎开 issue 讨论节点布局、缓存策略、持久化方案；提 PR 前请先说明要解决的问题。

## License

[MIT](LICENSE)，Copyright (c) 2026 Tyupoi-Chronoce。完整许可证文本见 [LICENSE](LICENSE)。

## 致谢

本项目起源于一次关于「路径即坐标」的讨论——从 `0xCC` 乱码开始，到跨分支路径重定向，再到把热路径缓存想象成神经突触。感谢所有后续愿意一起把它做对的人。

---

**语言：** [English](README.md) · 简体中文
