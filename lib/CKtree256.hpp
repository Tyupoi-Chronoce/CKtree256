#ifndef CKTREE256_HPP
#define CKTREE256_HPP

/*
 * CKtree256 -- experimental 256-way byte-radix tree.
 *
 * Public header of the C++ static library. The implementation lives in
 * CKtree256.cpp; include this header and link libCKtree256.a.
 *
 * 这是 C++ 静态库的公开头文件，实现位于 CKtree256.cpp；
 * 使用方只需包含本头文件并链接 libCKtree256.a。
 *
 * Design status: pre-alpha. The API is intentionally small and will change.
 * 设计状态：pre-alpha，接口会继续调整，不要用于生产。
 */

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace CKtree
{
    /*
     * Lower bound for the hot-path cache slot count. The constructor rounds
     * smaller requests up to this value.
     * 热路径缓存槽数的下限；构造时会把更小的请求提升到这个值。
     */
    inline constexpr std::size_t HVcache_minSize = 0xff;

    /*
     * Opaque node handle used by link_node() / get_linkNode().
     * The definition is private to the library; callers may pass a handle
     * around but cannot inspect a node. There is currently no public API
     * that hands out a fresh handle yet.
     *
     * link_node() / get_linkNode() 使用的节点句柄。完整定义在库内部，
     * 调用方只能传递指针，不能查看节点；目前还没有公开 API 能获取新句柄。
     */
    struct treeNode;

    /* Internal result type of the cache admission helper; defined in the .cpp. */
    /* 缓存准入 helper 的内部返回类型；完整定义在 .cpp 中。 */
    struct resData_32u;

    /*
     * A 256-way byte-radix tree, guarded by one internal mutex.
     *
     * Key convention: a key is (pointer, length), one byte per level.
     * A null pointer with length == 0 addresses the root; a null pointer with
     * length != 0 is invalid. The tree stores data pointers, never owns them.
     *
     * 一棵按字节分叉的 256 叉树，由一把内部互斥锁保护。
     * key 的约定：key = (指针, 长度)，每层消费一个字节；nullptr + 0 表示根，
     * nullptr + 非零长度非法；树只保存 data 指针，不拥有 data。
     */
    class Tree256
    {
    private:
        treeNode *rootNodeP;
        std::uint64_t visited_timesCount = 0; // counted for data I/O only / 仅统计 data I/O
        std::size_t size0f_HVcache;
        treeNode **HVcache_nodeP; // non-owning slot table, length size + 1 / 非拥有槽表
        bool *HVcache_aliveState;

        treeNode **tempNodeP_4easyFind = nullptr;

        std::mutex privateMutex;

        /*
         * The following helpers assume the caller already holds privateMutex.
         * 以下 private 辅助函数要求调用方已经持有 privateMutex。
         */
        resData_32u _can_putInto_cache(treeNode **nodeP = nullptr);
        bool _remove_from_cache(treeNode **nodeP);
        bool _node_isExist(std::uint8_t *nodePathP, std::size_t length);
        treeNode **_find_node(std::uint8_t *nodePathP, std::size_t length);
        bool _create_childrenArray(treeNode **nodeP);
        treeNode **_mount_node(std::uint8_t *nodePathP, std::size_t length);
        bool _free_nodeTree(treeNode **nodePP);
        bool _put2cache(std::uint8_t *nodePathP, std::size_t length);
        bool _put2cache(treeNode **nodeP);
        treeNode **_find_from_cache(std::size_t cacheIndex);
        std::size_t _find_from_cache(treeNode **nodeP);

    public:
        /*
         * Construct an empty tree.
         * @param HVcache_size requested slot count; values below HVcache_minSize
         *        are rounded up. Each slot stores one non-owning node pointer.
         *
         * 构造一棵空树；缓存槽数小于 HVcache_minSize 时会被提升到下限。
         */
        Tree256(std::size_t HVcache_size = HVcache_minSize);

        Tree256(const Tree256 &) = delete;
        Tree256 &operator=(const Tree256 &) = delete;

        /*
         * Allocate the 256-slot child array of an existing node.
         * Idempotent; returns false when the path is invalid or missing.
         *
         * 为已存在节点分配 256 槽子指针数组；幂等，路径非法或不存在时返回 false。
         */
        bool createChildren(std::uint8_t *nodePathP, std::size_t length);

        /*
         * Ensure the path exists, then write dataGP into the target node.
         * dataGP must be non-null and is non-owning; overwriting does not free
         * the previous pointer. A null path with length == 0 writes the root.
         *
         * 确保路径存在后写入 dataGP；dataGP 非空且归调用方所有，
         * 覆盖写不会释放旧指针；nullptr + 0 写入根节点。
         */
        bool put_data(std::uint8_t *nodePathP, std::size_t length, void *dataGP);

        /*
         * Return the data pointer at the path, or nullptr when the path is
         * missing or the node has no data. It cannot distinguish those two
         * cases yet. A null path with length == 0 reads the root.
         *
         * 返回路径上的 data 指针；路径不存在或节点没有 data 时返回 nullptr，
         * 目前无法区分这两种情况；nullptr + 0 读取根节点。
         */
        void *get_data(std::uint8_t *nodePathP, std::size_t length);

        /*
         * Clear the data pointer of an existing node; the node itself stays.
         * Returns false when the path is invalid or missing.
         *
         * 清空已存在节点的 data 指针，不删除节点；路径非法或不存在时返回 false。
         */
        bool erase_data(std::uint8_t *nodePathP, std::size_t length);

        /*
         * Link dstNodeP to the node at linkPathP by copying the path bytes.
         * The link is stored as a logical path, not as a raw target pointer;
         * a later link_node() on the same node replaces the old path.
         * dstNodeP must be an existing node handle.
         *
         * 把 dstNodeP 链接到 linkPathP 对应的节点，并复制路径字节；
         * 保存的是逻辑路径而不是裸目标指针；重复调用会替换旧路径；
         * dstNodeP 必须是已存在的节点句柄。
         */
        bool link_node(treeNode **dstNodeP, std::uint8_t *linkPathP, std::size_t length_linkedPath);

        /*
         * Resolve the node linked from nodeP. The first hit misses and walks
         * the path; once the source hit count passes the threshold, the target
         * node is admitted to the cache and later hits use the slot table.
         * On a cache hit the returned handle points at the cache slot, not at
         * the target's original parent slot; do not rely on pointer identity.
         * Returns nullptr for invalid arguments, a missing link, or a missing
         * target.
         *
         * 解析 nodeP 链接到的目标节点：首次走路径；源节点命中次数超过阈值后，
         * 目标节点被放入缓存，后续命中直接走槽表；缓存命中时返回的是缓存槽
         * 的 treeNode**，不是目标节点在父节点里的原槽，不要依赖指针身份；
         * 参数非法、链接缺失或目标不存在时返回 nullptr。
         */
        treeNode **get_linkNode(treeNode **nodeP);

        /*
         * Placeholder: subtree deletion is not implemented yet and always
         * returns false. Kept as a public API shape for the future.
         *
         * 占位接口：子树删除尚未实现，固定返回 false；保留为未来 API 形状。
         */
        bool free_node(std::uint8_t *nodePathP, std::size_t length, bool freeData);

        /*
         * Release all nodes, child arrays, external paths, and the cache table.
         * Does not delete dataP. The destructor takes privateMutex.
         *
         * 释放所有节点、子数组、external path 和缓存表；不删除 dataP；
         * 析构函数会持有 privateMutex。
         */
        ~Tree256();
    };
}

#endif // CKTREE256_HPP
