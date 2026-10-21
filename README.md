# CKtree256

一个实验性的 **256 叉基数树**（byte-radix tree）实现，目标是给树内/跨分支的节点引用找到一种「可持久化、跨进程、无悬垂指针」的表达方式——用**逻辑路径**代替裸指针，并用**热路径缓存**把跨分支跳转摊平成 O(1)。长期设想见设计文档 `introduction.md`（暂未公开，见 `.gitignore`）。

> **状态：pre-alpha，结构性原型。**
> 目前能按字节路径查找节点、分配子指针数组、递归释放整棵树。
> **还没有**插入/查找数据、跨分支跳转、缓存准入淘汰、持久化、单元测试和构建系统。
> 接口会大改，**不要用于生产**。

## 目录

| 文件 | 说明 |
|---|---|
| `classBasic.cpp` | 全部实现。目前是「头文件式 .cpp」（无 `main`、无 include guard），后续会拆成 `tree256.hpp` + `tree256.cpp` |
| `testMain.cpp` | 最小 smoke demo（构造 → 建子数组 → 查找 → 析构），见「编译与运行」 |
| `introduction.md` | 设计文档，本地保留、未入库 |

## 目前已经能做什么

- `createChildren(path, len)` —— 给路径上的节点分配 256 槽子指针数组（幂等，重复调用返回 true）
- `_node_isExist(path, len)` / `_find_node(path, len)` —— 按字节路径查找节点
- `_free_nodeTree(&slot)` —— 递归释放子树：子指针数组、external path、节点本体都释放，叶子不漏（实测 3 层树 + 外链路径，new/delete 计数 10/10 平衡）
- `HVcache_nodeP` —— `size + 1` 个槽的指针数组，槽 0 为哨兵（**只有槽表，准入/淘汰还没写**）
- `std::mutex Tree_Mutex` —— 占位，尚未接入任何接口

## 编译与运行

没有构建系统；`classBasic.cpp` 目前是「头文件式」实现，`testMain.cpp` 直接 `#include` 它。

```bash
# 只编译实现（检查是否能过编译）
g++ -std=c++20 -c classBasic.cpp -o classBasic.o

# 编译并运行 smoke demo
g++ -std=c++20 testMain.cpp -o cktree_demo
./cktree_demo
```

预期输出：

```
[demo] createChildren / _find_node / free_node 行为符合预期
[demo] TODO: insert、find-value、external path 跳转、HVcache 准入淘汰尚未实现
```

在 GCC 14.2.0 / MinGW-w64 上验证通过。

## TODO / 已知问题

按优先级排列，欢迎认领：

1. **接口未定型**：`free_node()`（public）还是空实现，`bool` 没有 `return`，调用即未定义行为；public API 的边界和返回语义待重新设计。
2. **插入与数据**：没有 insert / find-value；`dataP` 的所有权（树删还是调用方删）未定义；**没有 key 终结符约定**，`"AB"` 与 `"A"→"B"` 目前无法区分。
3. **缓存只有壳**：准入（`hit_count` 阈值）、淘汰（LRU / CLOCK）、pin（缓存中的节点不可删除）都未实现；`is_targetCached` / `index_4cache` 的语义（源节点记目标 vs 节点记自己）待定。
4. **跨分支引用未实现**：`externalRelatedLink_nodePathP` 只有字段，没有跳转、没有路径校验——结构变化后旧路径可能静默指向另一个节点，需要路径 hash/版本号。
5. **子节点表示内存放大**：每节点固定 256 个指针 = 2 KB，稀疏树浪费严重；计划 bitmap + 紧凑数组，或参考 ART 的 Node4/16/48/256。
6. **`tempNodeP` 隐式状态**：`_node_isExist` 用成员变量回传结果、`_find_node` 再读取它，可重入性和并发都脆弱；应改为直接返回槽指针。
7. **线程安全未验证**：`Tree_Mutex` 没有接入，锁粒度（整树锁 / 分段锁）未定。
8. **持久化还做不到「直接 dump」**：`childFP` 是裸指针，跨进程/落盘前必须改成 arena + 偏移，或序列化时转节点 id。
9. **没有正式测试与 CI**：`testMain.cpp` 只有 smoke demo，无单元测试框架、无 CMake、无 GitHub Actions。
10. **递归释放的深度 = key 长度**：作者已知并接受深 key 可能爆栈，后续可改显式栈。
11. **命名遗留**：`size0f_HVcache`、`length_externaRelatedlLink_nodePath`、`index_4cache` 等拼写/风格待统一。

## 设计要点

- 256 路分叉 = 一次消费一个字节，树深度等于 key 的字节数
- 跨分支引用保存目标节点的**字节路径**，而不是物理指针，避免悬垂指针
- 热路径命中次数超过阈值后，把目标节点的物理指针放进全局缓存池，实现 O(1) 跳转

## 贡献

当前阶段最需要的是**设计讨论**而不是代码量。欢迎开 issue 讨论节点布局、缓存策略、持久化方案；提 PR 前请先说明要解决的问题。

## License

[MIT](LICENSE)，Copyright (c) 2026 Tyupoi-Chronoce。

这意味着任何人都可以自由使用、修改、再分发、甚至商业使用，只需保留版权与许可声明。如果你之后不希望被商业使用，需要换成 AGPL-3.0 或 PolyForm Noncommercial 之类的协议——但**已经发布的版本无法收回授权**，只能对后续版本生效。

## 致谢

本项目起源于一次关于「路径即坐标」的讨论——从 `0xCC` 乱码开始，到跨分支路径重定向，再到把热路径缓存想象成神经突触。感谢所有后续愿意一起把它做对的人。
