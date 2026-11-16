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
 * Generated from classBasic.cpp (SHA256 ad23e0e0...874bc6) on 2026-11-16.
 * Regenerate this split after changing the development source.
 *
 * Design status: pre-alpha. The API is intentionally small and will change.
 * 设计状态：pre-alpha，接口会继续调整，不要用于生产。
 *
 * Exception contract: allocation failures in createChildren(), put_data() and
 * link_node() propagate as std::bad_alloc; invalid arguments / missing paths
 * return false. Callers that need a no-throw contract must catch std::bad_alloc.
 */

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace CKtree
{
    /* Lower bound for the hot-path cache slot count. */
    inline constexpr std::size_t HVcache_minSize = 0xff;

    struct treeNode;
    struct resData_32u;

    class Tree256
    {
    private:
        treeNode *rootNodeP;
        std::uint64_t visited_timesCount = 0; // just count for data I/O
        std::size_t size0f_HVcache;
        treeNode **HVcache_nodeP; // length -> size +1
        bool *HVcache_aliveState;
        std::uint32_t *HVcache_generation; // one generation per slot; changes on every assign/clear

        treeNode **tempNodeP_4easyFind = nullptr;

        std::mutex privateMutex;

        /*
            input-- a pointer pointed to a node
            output-- a struct data
            do what-- visit the HVcache and check the node if it can be put into the cache
            tips-- if it's already in the cache, return it current index;
                -- if it's not, put it in and return its index if there's a free slot, or return cant;
                -- caller must hold the private lock;
        */
        resData_32u _can_putInto_cache(treeNode** nodeP=nullptr);

        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- remove the node from the cache
            tips-- the infos of the node must be valid;
                -- caller must hold the private lock;
        */
        bool _remove_self_from_cache(treeNode* node);

        /*
            input-- a pointer to a node
            output-- true if the node had a cache record
            do what-- clear a node's record about its link target; does not evict the target
            tips-- the target may be shared by several source nodes, so it stays in the cache;
                -- caller must hold the private lock;
        */
        bool _forget_link_cache(treeNode* node);

        /*
            input-- path of node, length of nodePath
            output-- true if exist, false if not
            do what-- check if a node exists by the path
            tips-- if the node exist, will set the global tempNodeP, for easy find;
                -- caller must hold the private lock;
        */
        bool _node_isExist(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- path of node, length of nodePath
            output-- a pointer points to the node
            do what-- return the pointer points to the node if it exists, or nullptr
            tips-- nullptr path with length != 0 is invalid;
                -- caller must hold the private lock;
        */
        treeNode** _find_node(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- create a children-node-pointer array with nullptr-filled and mount it to the node
            tips-- if the node already points to a children-node-pointer array, it will do nothing and return true;
                -- caller must hold the private lock;
        */
        bool _create_childrenArray(treeNode** nodeP);

        /*
            input-- path of node, length of nodePath
            output-- a pointer points to the target node if success, or nullptr if args invalid
            do what-- create the children array and missing child node, ensure every node along the path exists
            tips-- length == 0 returns &rootNodeP;
                -- nullptr path with length != 0 is invalid;
                -- this func only ensures structure, it does not write dataP;
                -- caller must hold the private lock;
        */
        treeNode** _mount_node(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- recursively release the entire node tree rooted at this node
            tips-- if a node has no children-node-pointer array, will return true;
                -- release the node, its external path and its own cache slot;
                -- does not release datas, and does not release the nodes it links to;
                -- caller must hold the private lock;
        */
        bool _free_nodeTree(treeNode** nodePP);

        /*
            input-- path of node, length of nodePath
            output-- true if a new target was inserted into the cache, false otherwise
            do what-- resolve the node by path, then try to put its linked node into the cache
            tips-- nodePath -> nodeP -> externalRelatedLink_node as targetNode;
                -- the target is cached only when targetNode->hitCounts_4beJumped is above the threshold;
                -- if the targetNode is already in the cache, return false;
                -- caller must hold the private lock;
        */
        bool _put2cache(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- the pointer pointed to a node; optional targetNodeP resolved from its external link
            output-- true if a new target was inserted into the cache, false otherwise
            do what-- try to put the node linked by the specified node into the cache
            tips-- nodeP -> externalRelatedLink_node as targetNode;
                -- targetNodeP, when provided, must be the node resolved from nodeP's external link;
                -- the target is cached only when targetNode->hitCounts_4beJumped is above the threshold;
                -- if the targetNode is already in the cache, return false;
                -- caller must hold the private lock;
        */
        bool _put2cache(treeNode** nodeP, treeNode** targetNodeP = nullptr);

        /*
            input-- the index of the cache slot
            output-- a pointer points to the node
            do what-- return the pointer of the node if it exists in cache, or nullptr
        */
        treeNode** _find_from_cache(std::size_t cacheIndex);

        /*
            input-- a pointer pointed to a node
            output-- the index of the cache slot
            do what-- return the index of the cache slot if it exists, or return 0
        */
        std::size_t _find_from_cache(treeNode** nodeP);

        /*
            input-- the pointer pointed to the dst node, path of node to linked, length of nodePath
            output-- true if success, false if args invalid or link failed
            do what-- link a node to another node
            tips-- the dst and src nodes must exist;
                -- a later link_node on the same node overwrites the old pointer with freeing it;
                -- caller must hold the private lock;
        */
        bool _link_node(treeNode** dstNodeP, std::uint8_t* linkPathP, std::size_t length_linkedPath);


        /*
            input-- the pointer pointed to a node
            output-- a pointer pointes to a node if it exists, or nullptr
            do what-- return the pointer of the node linked by the node from input if it exists and args valid
            tips-- find from cache at first when the node from input says the link node is in cache;
                -- a cache hit requires a live slot with the same generation recorded by the source node;
                -- if the slot was cleared, evicted or reused, clear the source record and find it by path;
                -- caller must hold the private lock;
        */
        treeNode* _get_linkNode(treeNode** nodeP);

        /*
            input-- bools tha determines whether to perform the sweep operation and the downrade operation, uint8 tha as a penalty for be evicted
            output-- true if there're some nodes was evicted, or false
            do what-- traverse the cache and decide whether to evict some nodes
            tips-- the hit counts of nodes more than the minCount will not be evicted;
                -- if the hit_downrade mode on, the hit counts of nodes will sub 1 before comparing with the minCount;
                -- if the slots of cache is full and no nodes was evicted in first traversal, will evict the node with the least hit counts;
                -- if a node was evicted, the hit counts of it will sub the penalty value;
                -- if the alive state of a slot is false, the pointer of this slot will be set to nullptr;
                -- if the pointer of a slot is nullptr, the alive state of this slot will be set to false;
                -- caller must hold the private lock;
        */
        bool _cache_eviction(bool sweep=false, bool hit_downrade=false, std::uint8_t penalty=8u);

    public:
        //std::mutex publicMutex;

        Tree256(std::size_t HVcache_size=HVcache_minSize);

        Tree256(const Tree256 &) = delete;
        Tree256 &operator=(const Tree256 &) = delete;


        /*
            input-- path of node(must be exist), length of nodePath
            output-- true if success, false if not
            do what-- create a children-node-pointer array with nullptr-filled and mount it to the node
            tips-- if the node already points to a children-node-pointer array, it will do nothing and return true;
                -- the public API of the private func _create_childrenArray();
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        bool createChildren(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- path of node, length of nodePath, the global pointer of data
            output-- true if success, false if args invalid or mount failed
            do what-- ensure the path exists, then write dataGP into the target node
            tips-- dataGP is a non-owning pointer; the tree does not delete old/new data;
                -- the data must be maintained by the owner;
                -- a later put_data on the same path overwrites the old pointer without freeing it;
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        bool put_data(std::uint8_t* nodePathP, std::size_t length, void* dataGP);

        /*
            input-- path of node, length of nodePath
            output-- return the pointer of the data if it exists, or nullptr
            do what-- get the data of the node if it exists
            tips-- if the length == 0, return the data of the root node;
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        void* get_data(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- path of node, length of nodePath
            output-- true if success, false if args invalid or erase failed
            do what-- erase the data of the node if it exists
            tips-- if the length == 0, erase the data of the root node;
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        bool erase_data(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- path of node, length of nodePath, path of node to linked, length of link_nodePath
            output-- true if success, false if args invalid or link failed
            do what-- link a node to another node
            tips-- the dst and src nodes must exist;
                -- a later link_node on the same node overwrites the old pointer with freeing it;
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        bool link_node(std::uint8_t* dst_nodePathP, std::size_t from_dstNodePath, std::uint8_t* linkPathP, std::size_t length_linkedPath);

        /*
            input-- path of node, length of nodePath
            output-- a pointer pointes to a node if it exists, or nullptr
            do what-- return the pointer of the node linked by the node from input if it exists and args valid
            tips-- find from cache at first when the node from input says the link node is in cache;
                -- a cache hit requires a live slot with the same generation recorded by the source node;
                -- if the slot was cleared, evicted or reused, clear the source record and find it by path;
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        treeNode* get_linkNode(std::uint8_t* nodePathP, std::size_t length);

        /*
            input-- path of node, length of nodePath
            output-- true if success, false if args invalid or free failed
            do what-- free the node tree
            tips-- if a node has no children-node-pointer array, will return true;
                -- release the node, its external path and its own cache slot; does not release datas;
                -- cant release from the root node;
                -- public entry: takes privateMutex internally; do not call while already holding it;
        */
        bool free_nodeTree(std::uint8_t* nodePathP, std::size_t length);

        /*
            tips-- the destructor takes privateMutex internally; do not destroy while holding it;
                -- if the layer is too deep, it's recommended to manually release it in segments starting from the end;
        */
        ~Tree256();

    };
}

#endif // CKTREE256_HPP
