# CKtree256

一个实验性的 **256 叉基数树**（byte-radix tree）实现，目标是给树内/跨分支的节点引用找到一种「可持久化、跨进程、无悬垂指针」的表达方式——用**逻辑路径**代替裸指针，并用**热路径缓存**把跨分支跳转摊平成 O(1)。长期设想见设计文档 `introduction.md`（暂未公开，见 `.gitignore`）。

> **状态：pre-alpha，结构性原型。**
> 目前能创建子指针数组、按字节路径查找节点、递归释放整棵树；缓存只有「槽表 + 把目标节点放进空槽」的雏形。
> **还没有**插入/查找数据、跨分支跳转、缓存淘汰与 pin、持久化、单元测试和构建系统。
> 接口会大改，**不要用于生产**。

## 目录

| 文件 | 说明 |
|---|---|
| `classBasic.cpp` | 全部实现。目前是「头文件式 .cpp」（无 `main`、无 include guard），后续会拆成 `tree256.hpp` + `tree256.cpp` |
| `testMain.cpp` | 最小 smoke demo（构造 → 建子数组 → 查找 → 析构），见「编译与运行」 |
| `introduction.md` | 设计文档，本地保留、未入库 |
| `LICENSE` | MIT 许可证 |

## 目前已经能做什么

- `createChildren(path, len)` —— 给路径上的节点分配 256 槽子指针数组（幂等；路径不存在时安全返回 false）
- `_find_node(path, len)` / `_node_isExist(path, len)` —— 内部按字节路径查找节点；`_find_node` 返回父节点里指向目标节点的槽指针（根节点返回 `&rootNodeP`）
- `_free_nodeTree(&slot)` —— 递归释放子树：子指针数组、external path、节点本体都释放；不释放 `dataP`，叶子不漏（实测 3 层树 + 外链路径，new/delete 计数 10/10 平衡）
- `HVcache_nodeP` / `HVcache_aliveState` —— `size + 1` 个槽的 non-owning 指针表，槽 0 为哨兵；`_can_putInto_cache` / `_put2cache` 可以把 external link 的目标节点放进空闲槽
- `privateMutex` —— 保护树与缓存；`createChildren`、`_put2cache`、析构函数内部加锁，其它 private 辅助函数要求调用方持锁（见「线程与锁约定」）
- `free_node(path, len)` —— public 占位接口，目前固定返回 false，尚未实现（见 TODO）

## 编译与运行

没有构建系统；`classBasic.cpp` 目前是「头文件式」实现，`testMain.cpp` 直接 `#include` 它。

```bash
# 只编译实现（检查是否能过编译）
g++ -std=c++20 -Wall -Wextra -Wpedantic -c classBasic.cpp -o classBasic.o

# 编译并运行 smoke demo
g++ -std=c++20 -Wall -Wextra -Wpedantic testMain.cpp -o cktree_demo
./cktree_demo
```

预期输出：

```
[demo] createChildren / free_node 行为符合预期
[demo] TODO: insert、find-value、external path 跳转、HVcache 准入淘汰尚未实现
```

当前版本在 GCC 14.2.0 / MinGW-w64 上编译零警告，demo 退出码 0。

## TODO / 已知问题

按优先级排列，欢迎认领：

1. **接口未定型**：`free_node()`（public）还是占位实现，固定返回 false；目前 public 只有 `createChildren`（以及固定返回 false 的 `free_node` 占位），没有 public 的查找/插入接口，API 边界和返回语义待重新设计。
2. **插入与数据**：没有 insert / find-value；`dataP` 的所有权（树删还是调用方删）未定义；**没有 key 终结符约定**，`"AB"` 与 `"A"`→`"B"` 目前无法区分。
3. **缓存只有雏形**：没有淘汰（LRU / CLOCK）和 pin（缓存中的节点不可删除）；`minCounts_move2cache`、`linkNode_hitCounts` 还没有接入；`_can_putInto_cache` 线性扫描槽表、取第一个空槽，不区分「新放入」和「已被别的来源放入」，`_put2cache` 的 `true` 也可能只是「目标已在缓存」，而 `false` 既可能是「本来源已缓存」也可能是失败，返回语义需要拆分。节点释放时也只按 `self_in_cache` + 槽内指针校验来清槽，标志和槽不一致时可能留下悬垂槽。
4. **跨分支引用未实现**：`externalRelatedLink_nodePathP` 只有字段，没有跳转、没有路径校验——结构变化后旧路径可能静默指向另一个节点，需要路径 hash/版本号。
5. **子节点表示内存放大**：每节点固定 256 个指针 = 2 KB，稀疏树浪费严重；计划 bitmap + 紧凑数组，或参考 ART 的 Node4/16/48/256。
6. **`tempNodeP_4easyFind` 隐式状态**：`_node_isExist` 用成员变量回传结果、`_find_node` 再读取并清空；失败或提前返回时可能留下旧值，而且 `_find_node` 里还有一段实际上很难走到的重复遍历。应改为直接返回槽指针，不经过成员变量。
7. **线程安全未验证**：`privateMutex` 已接入 `createChildren`、`_put2cache` 和析构，`_find_node` / `_node_isExist` / `_can_putInto_cache` / `_free_nodeTree` 依赖调用方持锁；`free_node` 没有锁约定，锁粒度（整树锁 / 分段锁）也还没定。
8. **持久化还做不到「直接 dump」**：`childFP` 是裸指针，跨进程/落盘前必须改成 arena + 偏移，或序列化时转节点 id。
9. **没有正式测试与 CI**：`testMain.cpp` 只有 smoke demo，无单元测试框架、无 CMake、无 GitHub Actions。
10. **递归释放的深度 = key 长度**：作者已知并接受深 key 可能爆栈，后续可改显式栈。
11. **命名遗留**：`size0f_HVcache`、`length_externaRelatedlLink_nodePath`、`self_cacheIndex` 等拼写/风格待统一。

## 设计要点

- 256 路分叉 = 一次消费一个字节，树深度等于 key 的字节数
- 跨分支引用保存目标节点的**字节路径**，而不是物理指针，避免悬垂指针
- 热路径命中次数超过阈值后，把目标节点的物理指针放进树内缓存槽，实现 O(1) 跳转
- 缓存是 **non-owning** 的：节点被放进缓存后不能直接删除；淘汰 / pin 协议还没实现

## 线程与锁约定

- `privateMutex` 保护根指针、树结构和 HVcache 槽表。
- 已加锁的入口：`createChildren`、`_put2cache`、`~Tree256`。
- 要求调用方持锁的 private helper：`_find_node`、`_node_isExist`、`_can_putInto_cache`、`_free_nodeTree`（析构函数在持锁状态下递归调用 `_free_nodeTree`，递归本身不再加锁）。
- `free_node` 尚未实现，也没有锁约定；后续如果暴露 public 的查找/插入接口，必须把锁边界一起定下来。

## 贡献

当前阶段最需要的是**设计讨论**而不是代码量。欢迎开 issue 讨论节点布局、缓存策略、持久化方案；提 PR 前请先说明要解决的问题。

## License

[MIT](LICENSE)，Copyright (c) 2026 Tyupoi-Chronoce。

这意味着任何人都可以自由使用、修改、再分发、甚至商业使用，只需保留版权与许可声明。如果你之后不希望被商业使用，需要换成 AGPL-3.0 或 PolyForm Noncommercial 之类的协议——但**已经发布的版本无法收回授权**，只能对后续版本生效。

## 致谢

本项目起源于一次关于「路径即坐标」的讨论——从 `0xCC` 乱码开始，到跨分支路径重定向，再到把热路径缓存想象成神经突触。感谢所有后续愿意一起把它做对的人。
