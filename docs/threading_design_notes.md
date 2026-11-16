# 后台缓存淘汰线程：设计注意事项

> 状态：设计草稿，**尚未实现**。  
> 适用对象：计划在 `Tree256` 构造时启动、析构时停止的后台线程，周期性调用私有的 `_cache_eviction`。  
> 基准源码：`classBasic.cpp`（2026-11-16，721 行，SHA256 `ad23e0e0...874bc6`）。源码变化后请同步核对行号。

## 1. 现在有什么、没有什么

当前已经存在：

- `_cache_eviction(bool sweep=false, bool hit_downrade=false, uint8_t penalty=8u)`：在持有 `privateMutex` 的前提下遍历 HVcache，可以 sweep 冷槽、给被淘汰节点扣惩罚值，槽满且第一轮没淘汰时兜底淘汰命中次数最少的节点。位置约在 `classBasic.cpp:495`。
- `get_linkNode(path, len)`：public 入口，内部加锁，按源路径解析链接；目标命中次数超过阈值后调用 `_put2cache` 把目标放进 HVcache。位置约在 `classBasic.cpp:676`。
- `privateMutex`：保护根指针、树结构和 HVcache 槽表。public 入口每次持锁一次；private helper 要求调用方已经持锁。

当前**没有**：

- 没有任何代码调用 `_cache_eviction`；
- 没有后台线程、停止标志、条件变量或线程池；
- 没有 pin（固定缓存节点）协议，也没有公开的稳定节点句柄；
- 没有并发测试、TSAN/ASan 验证。

所以本文是“将来要加线程时别踩这些坑”的清单，不是对已实现行为的描述。

## 2. 推荐的生命周期

目标形态：一个 `Tree256` 实例最多一个 worker；构造完成后启动，析构时先停再释放。伪代码：

```cpp
class Tree256
{
    std::mutex privateMutex;
    std::condition_variable wakeCv;
    std::atomic<bool> stopRequested{false};
    std::thread worker;              // 或者 std::jthread
    std::chrono::milliseconds period{1000};
    bool workerStarted = false;

    void _evictionLoop()
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        while (!stopRequested.load(std::memory_order_relaxed))
        {
            wakeCv.wait_for(lock, period, [this] {
                return stopRequested.load(std::memory_order_relaxed);
            });
            if (stopRequested.load(std::memory_order_relaxed)) { break; }
            try
            {
                _cache_eviction(true, true, 8u);
            }
            catch (...)
            {
                // 后台线程不能把异常抛出去：std::terminate。
                // 记录错误/降级，具体策略由实现决定。
            }
        }
    }
};
```

构造顺序必须是：

1. 分配根节点、`HVcache_nodeP`、`HVcache_aliveState`、`HVcache_generation`；
2. 所有成员初始化完成；
3. **最后**启动 worker；
4. 如果 `std::thread` 构造抛出 `std::system_error`，用 RAII/unique_ptr 释放已分配内存，再决定是让构造失败还是降级为“无后台线程”。

不要先起线程、再继续分配缓存数组，否则 worker 可能看到半初始化的成员。

析构顺序必须是：

1. `stopRequested.store(true)`；
2. `wakeCv.notify_all()`；
3. **在拿 `privateMutex` 之前** `join()`；
4. 再 `std::unique_lock<std::mutex> lock(privateMutex)`；
5. `_free_nodeTree(&rootNodeP)`；
6. `delete[]` 三个 HVcache 数组。

第 3 步和第 4 步的顺序是硬约束：worker 在循环里持有 `privateMutex`。如果析构先拿锁再 join，worker 永远等不到锁，析构永远等不到 worker，直接死锁。

## 3. 锁与调用规则

- worker 只允许拿 `privateMutex`，不要在持锁时再拿第二把锁。将来若增加 link 注册表、统计锁等，必须写清全局锁顺序，并保证 worker 与 public 入口使用同一顺序。
- worker 内只调用“要求调用方持锁”的 private helper，例如 `_cache_eviction`、`_remove_self_from_cache`；不要调用 `get_linkNode`、`free_nodeTree` 这类自己加锁的 public 方法。
- public 入口不要在持锁时等待 worker、join 或 condition_variable，否则同样可能死锁。
- `hitCounts_4beJumped`、`self_in_cache`、`self_cacheIndex`、`linkNode_cacheIndex`、`linkNode_cacheGeneration` 以及三个 HVcache 数组都只能在持 `privateMutex` 时读写。不要为了“读一个计数”绕过锁；32 位读写本身原子不代表复合状态一致。
- 不要用 `sleep_for` 轮询：它不能被停止信号及时唤醒，析构可能要等一个完整周期。用 `condition_variable::wait_for(lock, period, predicate)`，停止时 `notify_all`。
- 条件变量的谓词要同时检查 stop 和周期；`wait_for` 可能虚假唤醒，返回后要重新判断。
- worker 的周期应当可配置，默认不要太短。淘汰本身要扫描整个槽表；高频扫描会和 public 操作抢 `privateMutex`。

## 4. 异常与失败路径

- 线程函数中任何未捕获异常都会触发 `std::terminate`。worker 体必须 `try/catch (...)`，至少把异常吞掉并记录状态。
- `_cache_eviction` 当前不分配内存，理论上只做指针和计数操作；将来若加入分配、日志或回调，要单独处理 `std::bad_alloc` 和回调异常。
- 线程创建失败（`std::system_error`）不能留下半启动状态：要么构造失败并释放资源，要么明确降级为“无后台淘汰”，并把状态暴露给调用方。
- 不要在 worker 里调用用户回调；如果将来必须调用，回调必须在锁外执行，并定义回调抛异常时的行为。

## 5. 返回指针的存活期

这是加线程后最容易出错的地方：

- `get_linkNode` 当前返回目标节点的裸 `treeNode*`，不是稳定句柄。它解锁后仍然指向树内节点；只要目标节点没有被 `free_nodeTree` 释放，指针就有效。
- 淘汰线程只清缓存槽，不删除节点，所以“缓存淘汰”本身不会让 `get_linkNode` 返回值悬垂；但另一个线程调用 `free_nodeTree` 会删除节点，此时调用方手里的指针就是悬垂指针。
- 现在没有 pin/引用计数，也没有“节点正在被外部使用”的登记。加后台线程之前，至少要明确：返回指针只在“调用方自己保证目标节点不会被其他线程 `free_nodeTree` 掉”时安全；单线程串行访问可以，跨线程共享则必须 pin 或改用稳定句柄。public `get_linkNode` 返回前已经释放了 `privateMutex`，不能靠“调用方持锁”来保护这个指针。
- 如果未来要跨线程安全地返回节点，建议改成稳定句柄（索引 + generation）或 `shared_ptr` 节点所有权；不要靠“淘汰线程不删节点”来掩盖 `free_nodeTree` 的竞争。

## 6. 线程模型与成本

- “每个 `Tree256` 一个 worker”实现简单，但实例多时线程数线性增长。大量小树建议共享一个调度器/线程池，或者只在显式开启时启动。
- 如果坚持一树一线程：构造/析构成本、栈空间（Windows 默认保留较大）、调试时线程数都会增加；README 和 API 文档要写明。
- `std::jthread`（C++20）可以自动 join 并提供 `stop_token`，但仍建议显式 `request_stop()` + `join()` 后再释放数组，避免“先释放、后停止”的错觉。用之前先在目标工具链上最小验证（本机 w64devkit GCC 14.2.0 可编译 `std::thread` / `std::jthread`；其他 ABI 不要假设）。
- 若使用 `std::jthread`，注意它的析构会自动 join；如果成员声明顺序导致数组先于 `jthread` 析构，仍可能出问题。成员声明顺序应保证 worker 先停止，再释放它访问的数据。

## 7. 测试清单

加线程后至少补这些测试（当前没有）：

1. 空树、满缓存、单热节点、冷热混合下反复启动/析构，确认不崩、不泄漏；
2. 构造后立刻析构，worker 还没进入循环就收到 stop；
3. worker 正在 `_cache_eviction` 时主线程析构，确认 join 不死锁；
4. 一个线程反复 `get_linkNode`，另一个线程反复 `free_nodeTree` / 重新链接，确认没有 use-after-free；
5. 高频 `put_data` / `erase_data` 与淘汰并发，确认 `hitCounts_4beJumped` 和槽状态一致；
6. 线程创建失败路径（可用注入/模拟）不会泄漏已分配数组；
7. 压力测试下 worker 周期、CPU 占用、锁等待时间可接受。

本机没有 ASan；访问冲突可能被 WER 挂起而不是立刻退出，测试程序应安装 `SetUnhandledExceptionFilter` / `SetErrorMode(SEM_NOGPFAULTERRORBOX)` 以便拿到 `0xC0000005`。

## 8. 常见错误对照表

| 错误写法 | 后果 | 正确做法 |
|---|---|---|
| 析构先锁 `privateMutex` 再 join | 死锁 | 先 stop + notify + join，再锁 |
| worker 调用 `get_linkNode` / `free_nodeTree` | 递归加锁死锁 | 只调用要求持锁的 private helper |
| 用 `sleep_for` 做周期 | 停止不及时、析构卡周期 | `wait_for` + 谓词 + `notify_all` |
| 异常逃出线程函数 | `std::terminate` | worker 体 `try/catch (...)` |
| 构造函数先起线程再分配数组 | worker 看到半初始化成员 | 最后启动 worker |
| 把 `get_linkNode` 返回的裸指针当稳定句柄 | `free_nodeTree` 后悬垂 | 定义 pin/句柄/所有权，或限制单线程使用 |
| 实例很多仍一树一线程 | 线程数爆炸 | 共享调度器/线程池，或显式开关 |

## 9. 落地顺序建议

1. 先让 `_cache_eviction` 有同步调用方（例如在 `_put2cache` 找不到空槽时调用一次），把淘汰策略跑通并补测试；
2. 再引入 worker，但只做“定时调用已有的同步路径”，不改变淘汰语义；
3. 最后处理返回指针/句柄的跨线程安全，必要时再设计 pin 或稳定句柄。

不要第一步就同时改“淘汰策略 + 后台线程 + 返回指针语义”，否则出问题很难定位。
