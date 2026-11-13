# CKtree256 C API 设计草案（v0.1-draft）

> 状态：设计草案，**未实现**；本文只记录方案，不修改 `Tree256`，也不提供 C ABI。
> 日期：2026-11-13
> 基线：HEAD `e91deb2 v0.1.6T`；`classBasic.cpp` 是开发源（663 行 / SHA256 `374fce76...56fb`），`_4lib_work` 是 2026-10-28 的旧库化快照，尚未同步 path 版 `link_node` / `free_nodeTree`。
> 注：当前工作区 `classBasic.cpp` 有未提交改动；本文以当前代码为准，未改动源码。

## 1. 目标与边界

目标：在不重构 `Tree256` 类的前提下，给 C / FFI 调用方提供稳定的不透明句柄接口：

- 类实例由库内部注册表管理，C 侧只拿 handle；
- C 侧不接触 `Tree256` 布局、`std::mutex`、`treeNode` 或任何 C++ 类型；
- C++ 类接口保留为内部/开发接口，C ABI 是最终对外接口；
- 每个 C 入口先校验 handle，再转发到类实例；
- C++ 异常不允许穿过 C ABI。

非目标（本版不做）：

- 不包装 `get_linkNode`（仍缺公开 node handle；见 `4.1`）；`link_node` 已有 path 版，但是否进入 v1 见开放问题。
- C ABI v1 不包装 `free_nodeTree`（类里已实现，但 `freeData` / 删除粒度未定）；
- 不解决路径失效、缓存淘汰/pin、持久化或跨进程；
- 不把 C ABI 冻结为最终版本，先以 `CKTREE_C_API_VERSION 1` 为草案。

## 2. 句柄方案

### 2.1 需求

句柄必须满足：

1. 外部无法通过句柄直接解引用或改写库内对象；
2. 伪造、随机、已销毁的句柄必须被拒绝；
3. 销毁后槽位复用，旧句柄不能误操作新实例；
4. 校验成本固定，不能每次调用线性扫描整个实例数组；
5. 句柄在 C ABI 中是固定宽度类型，不依赖指针宽度。

### 2.2 备选方案

| 方案 | 校验 | 主要问题 |
|---|---|---|
| `void*` = `Tree256*`，注册表查找 | 数组/哈希表 | 挡不住释放后地址复用；旧句柄可能变成新树；线性扫描 O(n) |
| `void**` = `&slots[i]` | 固定数组范围 + 槽状态 | 槽复用后旧 handle 指向新实例；句柄不带 generation；`*handle` 还可被调用方改写 |
| `uint64_t` = index + generation | 查固定槽表 | 推荐；无裸指针、旧 handle 永久失效、O(1) |

结论：**最终 ABI 用 `uint64_t`，低 32 位放 `index + 1`，高 32 位放 `generation`，0 表示无效。** 不把 `Tree256*` 当句柄，也不把 `void**` 槽地址当最终 handle。

### 2.3 句柄编码

~~~text
bit 63..32 : generation (32-bit)
bit 31..0  : index + 1 (0 保留给 invalid handle)
~~~

- 槽位数组固定长度 `CKTREE_MAX_INSTANCES`，默认可以从 64 开始；数组不扩容，保证槽地址稳定。
- 每个槽：`shared_ptr<Tree256> tree; uint32_t generation; bool alive;`。
- `create` 找空闲槽，`generation++`（回绕时退役该槽或返回失败），分配实例，返回 `(generation << 32) | (index + 1)`。
- `resolve` 先检查 `index < capacity`、`alive` 和 `generation` 完全一致，再复制 `shared_ptr` 给调用方。
- `destroy` 在注册表锁内标记 `alive = false`、取出 `shared_ptr`，锁外释放；槽可复用，复用时 `generation` 递增，旧 handle 失效。
- `generation` 回绕（约 2^32 次复用）时退役槽，避免旧 handle 偶然重新匹配。

### 2.4 为什么不复用 `void*` / `void**`

- `void*` = 实例地址：数组校验只能证明“这个地址现在在表里”，不能证明“调用方拿的是当前这一代”；地址被分配器复用后，旧句柄会静默操作新树。
- `void**` = 槽地址：固定数组时范围校验是 O(1)，但句柄值本身不带 generation；槽内 generation 只能说明槽当前有效，不能说明调用方持有的是旧代。且外部可以写 `*handle`，库不能信任它。
- 如果某个 FFI 强制要求 `void*`，只在最外层做 `(void*)(uintptr_t)handle` 的边界转换，仍不要把 `Tree256*` 放进去；整数↔指针转换是 implementation-defined，`uint64_t` 更干净。

## 3. 注册表与生命周期

### 3.1 槽表

~~~cpp
struct Slot {
    std::shared_ptr<Tree256> tree;
    std::uint32_t generation = 0;
    bool alive = false;
};

std::array<Slot, CKTREE_MAX_INSTANCES> g_slots;
std::mutex g_registryMutex;
~~~

- `create`：在锁内找空槽并预留；构造 `Tree256` 可能抛异常，必须捕获；失败返回 0，不留下半初始化槽。
- `resolve`：锁内校验并复制 `shared_ptr`，锁外调用类方法；不要持注册表锁执行 `put/get`，否则所有实例被串行化。
- `destroy`：锁内移除并取走 `shared_ptr`，锁外释放；正在执行的调用持有自己的 `shared_ptr`，对象不会 UAF。
- 同一实例的并发调用由 `Tree256` 内部的 `privateMutex` 串行化；C wrapper 不再加第二把实例锁。

### 3.2 线程安全边界

- `Tree256` 自己的锁保护树结构和缓存；
- 注册表锁只保护 `slots`、`alive`、`generation` 和 `tree` 指针；
- `destroy` 与 `get/put` 并发：已拿到 `shared_ptr` 的调用允许完成；后续 `resolve` 失败；
- 不承诺 `destroy` 会中断正在执行的调用；需要“立即取消”要另加取消机制。

## 4. C ABI 草案

### 4.1 当前代码约束

- `link_node` 现在有 path 版目标入口（`classBasic.cpp:563`），但 `get_linkNode` 仍要求 `treeNode**`（`classBasic.cpp:580`），没有公开 node handle；C 侧目前只能“写链接”，不能“读链接”，所以 v1 不包含 link。
- `free_nodeTree` 已实现（`classBasic.cpp:634`），会递归释放子树、external path 和节点自己的缓存槽；但它不释放 `dataP`，也不允许释放根节点。`put_data` 的 `dataP` 是非拥有指针（`classBasic.cpp:32`、`:499-501`），`freeData` 和删除粒度未定，所以 C 层只提供整树 `destroy`。
- 当前 `get_data` 对“路径缺失”和“节点存在但无 data”都返回 `nullptr`；C 层第一版继承这个限制，不假装能区分。
- `smoke.cpp` 已覆盖当前 public API 的冒烟测试（root/key/prefix/embedded 0/invalid args/createChildren/link_node 参数检查/free_nodeTree）；`testMain.cpp` 已移交作者作为自用实测入口。README 和本文已按当前 API 更新；`_4lib_work` / `lib` 仍是旧快照。

### 4.2 C 头文件草案

~~~c
#ifndef CKTREE256_C_H
#define CKTREE256_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CKTREE_C_API_VERSION 1

#if defined(_WIN32)
#  if defined(CKTREE_BUILD_DLL)
#    define CKTREE_API __declspec(dllexport)
#  elif defined(CKTREE_USE_DLL)
#    define CKTREE_API __declspec(dllimport)
#  else
#    define CKTREE_API
#  endif
#else
#  define CKTREE_API
#endif

typedef uint64_t cktree_handle;
typedef int cktree_status;

#define CKTREE_INVALID_HANDLE 0

#define CKTREE_OK                    0
#define CKTREE_ERR_INVALID_HANDLE    1
#define CKTREE_ERR_INVALID_ARGUMENT  2
#define CKTREE_ERR_NO_MEMORY         3
#define CKTREE_ERR_CAPACITY          4
#define CKTREE_ERR_NOT_FOUND         5
#define CKTREE_ERR_INTERNAL          6

CKTREE_API uint32_t cktree_c_api_version(void);

CKTREE_API cktree_handle cktree_create(size_t cache_slots);
CKTREE_API cktree_status cktree_destroy(cktree_handle handle);

CKTREE_API cktree_status cktree_put(cktree_handle handle,
                                    const uint8_t* key,
                                    size_t key_len,
                                    void* data);

CKTREE_API cktree_status cktree_get(cktree_handle handle,
                                    const uint8_t* key,
                                    size_t key_len,
                                    void** out_data);

CKTREE_API cktree_status cktree_erase(cktree_handle handle,
                                      const uint8_t* key,
                                      size_t key_len);

CKTREE_API cktree_status cktree_create_children(cktree_handle handle,
                                                const uint8_t* key,
                                                size_t key_len);

#ifdef __cplusplus
}
#endif

#endif /* CKTREE256_C_H */
~~~

### 4.3 语义约定

- `cktree_create(cache_slots)`：失败返回 0；实例数满返回 0，不设置全局错误（是否加 `cktree_last_error` 见开放问题）。
- `cktree_destroy`：销毁整棵树和节点；不释放 `data`（树从未拥有 data）；无效/已销毁 handle 返回 `CKTREE_ERR_INVALID_HANDLE`。
- `cktree_put`：`data == nullptr` 返回 `CKTREE_ERR_INVALID_ARGUMENT`；重复 put 只覆盖指针，不释放旧 data。
- `cktree_get`：`out_data` 不能为空；找不到时返回 `CKTREE_OK` 且 `*out_data = nullptr`（与当前类语义一致，不区分 missing/no-data）。
- `cktree_erase`：找不到返回 `CKTREE_ERR_NOT_FOUND`；成功清除 data 并返回 OK。
- `cktree_create_children`：已存在子数组时幂等成功；路径不存在返回 `CKTREE_ERR_NOT_FOUND`。
- key 为 `nullptr + len == 0` 表示根；`nullptr + len > 0` 非法；非空 key 的 `len` 是唯一路径长度，不依赖 C 字符串终止符。
- 所有 `cktree_handle` 参数先校验；无效 handle 不进入 `Tree256`，不解引用任何旧指针。

### 4.4 异常与错误

- 所有 C 入口用 `try/catch` 包住：`std::bad_alloc` → `CKTREE_ERR_NO_MEMORY`；其他异常 → `CKTREE_ERR_INTERNAL`；C++ 异常不得穿过 C ABI。
- `cktree_create` 返回句柄而不是 status，因此内存不足/容量满都只能返回 0；如果需要细分，加 `cktree_last_error()`（线程局部），本版先不做。
- 不放 `bool` 到 ABI；status 用 `int` + 宏常量，避免 C enum 宽度差异。

## 5. 构建与目录

建议文件布局：

~~~text
_4lib_work/CKtree256_c.h      C 公开头文件（或放 lib/）
_4lib_work/CKtree256_c.cpp    C ABI 实现，include CKtree256.hpp
lib/CKtree256_c.h             对外分发的头文件副本
lib/libCKtree256_c.a          可选：独立 C ABI 静态库
~~~

- C ABI 与 C++ 类实现分离；`CKtree256.hpp` 不暴露给 C 调用方。
- 可以合并进现有 `lib/libCKtree256.a`，但推荐单独 `libCKtree256_c.a`，让 C 调用方明确链接的是 C 层；是否合并留到实现时定。
- C 调用方用 C 编译器编译；最终链接仍需 C++ 运行时（MinGW 下用 `g++` 链接，或显式 `-lstdc++`）。
- 如果做 DLL，C 符号用 `__declspec(dllexport/dllimport)`；C++ 符号可以隐藏，C ABI 是唯一稳定入口。
- 预留 `CKTREE_C_API_VERSION`；任何破坏性语义变更升版本。

## 6. 第一版范围

包含：

- `create / destroy / put / get / erase / create_children / c_api_version`。

暂不包含：

- `get_linkNode`：仍缺 path 版读取；`link_node` 的 path 版已存在，可在 v1.1 包 `cktree_link_path`，但配套读取 API 未定。
- `free_nodeTree`：虽然类里已实现，C ABI v1 仍不包装；先定 data deleter、单节点/子树粒度和缓存/外链失效。
- 缓存淘汰/pin、持久化、跨进程、节点压缩。

## 7. 测试计划（实现时）

1. C 编译器（`gcc -std=c11`）单独 include `CKtree256_c.h`，确认头文件没有 C++ 类型。
2. `create/destroy` 基本生命周期；`destroy` 后旧 handle 必须返回 `INVALID_HANDLE`。
3. `destroy` 后创建新实例复用槽，旧 handle 仍然失败（generation 校验）。
4. 容量满：连续 `create` 到上限，下一次返回 0；`destroy` 后可复用。
5. `put/get/erase`：根、空 key、嵌入 0、重复 put、缺失路径、`nullptr + len > 0`、空 data。
6. `get` 的 `out_data == nullptr` 参数校验；missing/no-data 都返回 OK + null。
7. 数据所有权：destroy 不释放 data；重复 put 不释放旧 data。
8. 并发：一个线程重复 `get`，另一个线程 `destroy`，不得崩溃/泄漏（可先做压力 smoke，不引入测试框架）。
9. 资源：ASan 不可用（本机已知），用计数/probe 或 Valgrind 替代；析构后无泄漏/无挂起。

## 8. 开放问题

- `CKTREE_MAX_INSTANCES` 默认值；固定容量还是可配置容量。
- `generation` 32 位回绕策略：退役槽/失败/更宽字段。
- `cktree_create` 失败是否需要 `cktree_last_error`；错误信息是否线程局部。
- 是否必须支持 `void*` handle 的 FFI；如果必须，是否接受边界转换。
- 独立 `libCKtree256_c.a` 还是合并进现有 `libCKtree256.a`。
- 命名前缀用 `cktree_` 还是 `CKtree_`；DLL 名称和导出宏。
- 已存在的 path 版 `link_node` 是否在 v1 包成 `cktree_link_path`；`get_linkNode` 何时补 path 读取和节点身份（`node_id/generation`）。
- `free_nodeTree` 的 data deleter 回调、子树/单节点语义、缓存和外链失效。
- 是否需要 C11 `stdbool`/`bool`；本草案统一用 `int` 返回状态。
- 是否需要 ABI 兼容性测试（C 调用方 + C++ 库/不同编译器）。

## 9. 实施状态

- **未实现**；本文只作为后续实现的设计基线。
- 未修改 `classBasic.cpp`、`_4lib_work` 或 `lib`；没有新增 C 头文件/实现文件。
- `smoke.cpp` 已更新为当前 public API 的冒烟测试；`testMain.cpp` 归作者自用。
- 启动条件：需要 C/FFI 调用方，或准备冻结对外 ABI；在此之前优先补公开 node handle / path 读取、路径失效校验和 `free_nodeTree` 的 data deleter 语义。
