#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <new>
#include <memory>



namespace CKtree
{

    static const size_t children_size = 256;
    static const size_t HVcache_minSize = 0xff;
    static const uint32_t minCounts_move2cache = 0x0f; // just can when tha bigger than it
    static const uint32_t deadNum_32u = 0xdeadc01d;

    static uint32_t _nextCacheGeneration(uint32_t generation)
    {
        ++generation;
        return generation == 0 ? 1u : generation;
    }


    /*
        the basic node of the tree;
        the status of the linkNode will be maintained at the next visit runtime;
    */
    template <typename Tname>
    struct treeNode
    {
        Tname *dataP = nullptr; // data is from the user, so must be released by the user and the type ofit is user's responsibility
        treeNode<Tname> **childFP = nullptr;
        uint8_t *externalRelatedLink_nodePathP = nullptr;
        bool has_externalLink = false;
        bool is_linkNode_targetCached = false; // for the externalRelatedLink_node
        bool self_in_cache = false;
        uint32_t linkNode_cacheIndex = 0; // index ==0 -> nullptr(cached not), for the externalRelatedLink_node, too 
        uint32_t linkNode_cacheGeneration = 0; // cache-slot generation this index referred to
        uint32_t linkNode_hitCounts = 0; // count the hit(jumped) times for the externalRelatedLink_node
        uint32_t self_cacheIndex = 0;
        size_t length_externaRelatedlLink_nodePath = 0; // nomal length of array
        //uint64_t node_id;
    };

    struct resData_32u
    {
        uint32_t value32u = deadNum_32u;
        bool is_valid = false;
        bool is_new = false;
    };
    struct resData_sizet
    {
        size_t value = 0;
        bool is_valid = false;
    };


    template <typename typeG>
    class Tree256
    {
    private:
        treeNode<typeG> *rootNodeP;
        uint64_t visited_timesCount = 0; // just count for data I/O
        size_t size0f_HVcache;
        treeNode<typeG> **HVcache_nodeP; // length -> size +1
        bool *HVcache_aliveState;
        uint32_t *HVcache_generation; // one generation per slot; changes on every assign/clear

        treeNode<typeG> **tempNodeP_4easyFind = nullptr;
        
        std::mutex privateMutex;

        /*
            input-- a pointer pointed to a node
            output-- a struct data
            do what-- visit the HVcache and check the node if it can be put into the cache
            tips-- if it's already in the cache, return it current index;
                -- if it's not, put it in and return its index if there's a free slot, or return cant;
                -- caller must hold the private lock;
        */
        resData_32u _can_putInto_cache(treeNode<typeG>** nodeP=nullptr)
        {
            resData_32u res;
            if (nodeP == nullptr || *nodeP == nullptr) {return res; } // cant

            for (size_t index =1; index <= size0f_HVcache; ++index)
            {
                if (*nodeP == HVcache_nodeP[index]) // check if it's already in the cache
                {
                    res.value32u = static_cast<uint32_t>(index);
                    res.is_valid = true;
                    res.is_new = false;
                    break;
                }
                else if (HVcache_aliveState[index] == false && res.is_valid == false) // check if there's a free slot
                {
                    res.value32u = static_cast<uint32_t>(index);
                    res.is_valid = true;
                }
            }
            if (res.is_valid) // can put it in
            {
                const uint32_t index = res.value32u;
                if (!HVcache_aliveState[index] || HVcache_nodeP[index] != *nodeP)
                {
                    HVcache_nodeP[index] = *nodeP;
                    HVcache_aliveState[index] = true;
                    HVcache_generation[index] = _nextCacheGeneration(HVcache_generation[index]);
                    res.is_new = true;
                }
                (*nodeP)->self_in_cache = true;
                (*nodeP)->self_cacheIndex = index;
            }
            return res;
        }

        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- remove the node from the cache
            tips-- the infos of the node must be valid;
                -- caller must hold the private lock;
        */
        bool _remove_self_from_cache(treeNode<typeG>* node)
        {
            if (node == nullptr) {return false; }
            if (!node->self_in_cache)
            {
                node->self_cacheIndex = 0;
                return false;
            }

            const uint32_t cacheIndex = node->self_cacheIndex;
            node->self_in_cache = false;
            node->self_cacheIndex = 0;
            if (cacheIndex == 0 || cacheIndex > size0f_HVcache) {return false; }
            if (HVcache_aliveState[cacheIndex] == false || HVcache_nodeP[cacheIndex] != node) {return false; }
            
            HVcache_aliveState[cacheIndex] = false;
            HVcache_nodeP[cacheIndex] = nullptr;
            HVcache_generation[cacheIndex] = _nextCacheGeneration(HVcache_generation[cacheIndex]);
            return true;
        }

        /*
            input-- a pointer to a node
            output-- true if the node had a cache record
            do what-- clear a node's record about its link target; does not evict the target
            tips-- the target may be shared by several source nodes, so it stays in the cache;
                -- caller must hold the private lock;
        */
        bool _forget_link_cache(treeNode<typeG>* node)
        {
            if (node == nullptr) {return false; }
            const bool hadRecord = node->is_linkNode_targetCached || node->linkNode_cacheIndex != 0;
            node->is_linkNode_targetCached = false;
            node->linkNode_cacheIndex = 0;
            node->linkNode_cacheGeneration = 0;
            return hadRecord;
        }

        /*
            input-- path of node, length of nodePath
            output-- true if exist, false if not
            do what-- check if a node exists by the path
            tips-- if the node exist, will set the global tempNodeP, for easy find;
                -- caller must hold the private lock;
        */
        bool _node_isExist(uint8_t* nodePathP, size_t length)
        {
            treeNode<typeG> **nodeTP = &rootNodeP;
            if (nodePathP == nullptr) {return false; }
            if (rootNodeP == nullptr) {return false; }
            for (size_t index =0; index < length; ++index)
            {
                if ((*nodeTP)->childFP == nullptr) {return false; }
                uint8_t nodeIndex = nodePathP[index];
                if ((*nodeTP)->childFP[nodeIndex] == nullptr) {return false; }
                nodeTP = &((*nodeTP)->childFP[nodeIndex]);
            }
            tempNodeP_4easyFind = nodeTP;
            return true;
        }
         
        /*
            input-- path of node, length of nodePath
            output-- a pointer points to the node
            do what-- return the pointer points to the node if it exists, or nullptr
            tips-- nullptr path with length != 0 is invalid;
                -- caller must hold the private lock;
        */
        treeNode<typeG>** _find_node(uint8_t* nodePathP, size_t length)
        {
            if (length == 0) {return &rootNodeP; }
            tempNodeP_4easyFind = nullptr;
            if (!_node_isExist(nodePathP, length)) {return nullptr; }
            treeNode<typeG> **nodeTP = &rootNodeP;
            if (tempNodeP_4easyFind != nullptr)
            {
                nodeTP = tempNodeP_4easyFind;
                tempNodeP_4easyFind = nullptr;
            }
            else
            {
                for (size_t index =0; index < length; ++index)
                {
                    if ((*nodeTP)->childFP == nullptr) {return nullptr; }
                    uint8_t nodeIndex = nodePathP[index];
                    if ((*nodeTP)->childFP[nodeIndex] == nullptr) {return nullptr; }
                    nodeTP = &((*nodeTP)->childFP[nodeIndex]);
                }
            }
            return nodeTP;
        }

        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- create a children-node-pointer array with nullptr-filled and mount it to the node
            tips-- if the node already points to a children-node-pointer array, it will do nothing and return true;
                -- caller must hold the private lock;
        */
        bool _create_childrenArray(treeNode<typeG>** nodeP)
        {
            if (nodeP == nullptr || *nodeP == nullptr) {return false; }
            if ((*nodeP)->childFP != nullptr) {return true; }
            (*nodeP)->childFP = new treeNode<typeG> *[children_size]();
            return true;
        }

        /*
            input-- path of node, length of nodePath
            output-- a pointer points to the target node if success, or nullptr if args invalid
            do what-- create the children array and missing child node, ensure every node along the path exists
            tips-- length == 0 returns &rootNodeP;
                -- nullptr path with length != 0 is invalid;
                -- this func only ensures structure, it does not write dataP;
                -- caller must hold the private lock;
        */
        treeNode<typeG>** _mount_node(uint8_t* nodePathP, size_t length)
        {
            if (nodePathP == nullptr && length != 0) {return nullptr; }
            treeNode<typeG> **nodeLP = &rootNodeP;
            if (*nodeLP == nullptr) {return nullptr; }

            for (size_t index =0; index < length; ++index)
            {
                if (!_create_childrenArray(nodeLP)) {return nullptr; }

                treeNode<typeG> **nodeTP = &((*nodeLP)->childFP[nodePathP[index]]);
                if (*nodeTP == nullptr) {*nodeTP = new treeNode<typeG>(); }
                
                nodeLP = nodeTP;
            }
            return nodeLP;
        }
        
        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- recursively release the entire node tree rooted at this node
            tips-- if a node has no children-node-pointer array, will return true;
                -- release the node, its external path and its own cache slot;
                -- does not release datas, and does not release the nodes it links to;
                -- caller must hold the private lock;
        */
        bool _free_nodeTree(treeNode<typeG>** nodePP)
        {
            if (nodePP == nullptr || *nodePP == nullptr) {return false; }
            treeNode<typeG> *nodeP = *nodePP;
            *nodePP = nullptr;

            _remove_self_from_cache(nodeP);

            if (treeNode<typeG> **children = nodeP->childFP)
            {
                for (size_t index = 0; index < children_size; ++index)   
                {
                    _free_nodeTree(&children[index]);          
                }
                delete[] children;
                nodeP->childFP = nullptr;
            }
            delete[] nodeP->externalRelatedLink_nodePathP; 
            nodeP->externalRelatedLink_nodePathP = nullptr;
            delete nodeP;

            return true;
        }

        /*
            input-- path of node, length of nodePath
            output-- true if success, false if not
            do what-- try to put the node linked by the specified node into the cache
            tips-- nodePath -> nodeP -> externalRelatedLink_node as targetNode -> put it into the cache;
                -- if the targetNode is already in the cache, return false;
                -- if the hit counts of targetNode is not enough, return false;
                -- caller must hold the private lock;
        */
        bool _put2cache(uint8_t* nodePathP, size_t length)
        {
            //std::unique_lock<std::mutex> lock(privateMutex);
            treeNode<typeG> **nodeP = _find_node(nodePathP, length);
            if (nodeP == nullptr || *nodeP == nullptr) {return false; }
            if ((*nodeP)->externalRelatedLink_nodePathP == nullptr
                 || (*nodeP)->length_externaRelatedlLink_nodePath == 0) {return false; }
            if ((*nodeP)->linkNode_hitCounts <= minCounts_move2cache) {return false; }

            treeNode<typeG> **targetNodeP = _find_node((*nodeP)->externalRelatedLink_nodePathP, (*nodeP)->length_externaRelatedlLink_nodePath);
            if (targetNodeP == nullptr || *targetNodeP == nullptr) {return false; }

            // Only trust linkNode_cacheIndex while its slot still points at this target.
            const uint32_t oldIndex = (*nodeP)->linkNode_cacheIndex;
            if (oldIndex != 0 && oldIndex <= size0f_HVcache && HVcache_aliveState[oldIndex]
                && HVcache_nodeP[oldIndex] == *targetNodeP)
            {
                (*nodeP)->is_linkNode_targetCached = true;
                (*nodeP)->linkNode_cacheIndex = oldIndex;
                (*nodeP)->linkNode_cacheGeneration = HVcache_generation[oldIndex];
                return false; // target is already cached
            }
            (*nodeP)->linkNode_cacheIndex = 0;
            (*nodeP)->linkNode_cacheGeneration = 0;
            (*nodeP)->is_linkNode_targetCached = false;

            resData_32u res = _can_putInto_cache(targetNodeP);
            if (res.is_valid) // can put it in
            {
                (*nodeP)->is_linkNode_targetCached = true;
                (*nodeP)->linkNode_cacheIndex = res.value32u;
                (*nodeP)->linkNode_cacheGeneration = HVcache_generation[res.value32u];
                return res.is_new;
            }
            return false;
        }

        /*
            input-- the pointer pointed to a node
            output-- true if success, false if not
            do what-- try to put the node linked by the specified node into the cache
            tips-- nodePath -> nodeP -> externalRelatedLink_node as targetNode -> put it into the cache;
                -- if the targetNode is already in the cache, return false;
                -- if the hit counts of targetNode is not enough, return false;
                -- caller must hold the private lock;
        */
        bool _put2cache(treeNode<typeG>** nodeP)
        {
            if (nodeP == nullptr || *nodeP == nullptr) {return false; }
            //std::unique_lock<std::mutex> lock(privateMutex);
            if ((*nodeP)->externalRelatedLink_nodePathP == nullptr
                 || (*nodeP)->length_externaRelatedlLink_nodePath == 0) {return false; }
            if ((*nodeP)->linkNode_hitCounts <= minCounts_move2cache) {return false; }

            treeNode<typeG> **targetNodeP = _find_node((*nodeP)->externalRelatedLink_nodePathP, (*nodeP)->length_externaRelatedlLink_nodePath);
            if (targetNodeP == nullptr || *targetNodeP == nullptr) {return false; }

            // Only trust linkNode_cacheIndex while its slot still points at this target.
            const uint32_t oldIndex = (*nodeP)->linkNode_cacheIndex;
            if (oldIndex != 0 && oldIndex <= size0f_HVcache && HVcache_aliveState[oldIndex]
                && HVcache_nodeP[oldIndex] == *targetNodeP)
            {
                (*nodeP)->is_linkNode_targetCached = true;
                (*nodeP)->linkNode_cacheIndex = oldIndex;
                (*nodeP)->linkNode_cacheGeneration = HVcache_generation[oldIndex];
                return false; // target is already cached
            }
            (*nodeP)->linkNode_cacheIndex = 0;
            (*nodeP)->linkNode_cacheGeneration = 0;
            (*nodeP)->is_linkNode_targetCached = false;

            resData_32u res = _can_putInto_cache(targetNodeP);
            if (res.is_valid) // can put it in
            {
                (*nodeP)->is_linkNode_targetCached = true;
                (*nodeP)->linkNode_cacheIndex = res.value32u;
                (*nodeP)->linkNode_cacheGeneration = HVcache_generation[res.value32u];
                return res.is_new;
            }
            return false;
        }

        /*
            input-- the index of the cache slot
            output-- a pointer points to the node
            do what-- return the pointer of the node if it exists in cache, or nullptr
        */
        treeNode<typeG>** _find_from_cache(size_t cacheIndex)
        {
            if (cacheIndex == 0 || cacheIndex > size0f_HVcache) {return nullptr; }
            if (HVcache_aliveState[cacheIndex] == false || HVcache_nodeP[cacheIndex] == nullptr) {return nullptr; }

            return HVcache_nodeP + cacheIndex;
        }

        /*
            input-- a pointer pointed to a node
            output-- the index of the cache slot
            do what-- return the index of the cache slot if it exists, or return 0
        */
        size_t _find_from_cache(treeNode<typeG>** nodeP)
        {
            if (nodeP == nullptr || *nodeP == nullptr) {return 0; }
            for (size_t index =1; index <= size0f_HVcache; ++index)
            {
                if (HVcache_aliveState[index] && HVcache_nodeP[index] == *nodeP) {return index; }
            }
            return 0;
        }

        /*
            input-- the pointer pointed to the dst node, path of node to linked, length of nodePath
            output-- true if success, false if args invalid or link failed
            do what-- link a node to another node
            tips-- the dst and src nodes must exist;
                -- a later link_node on the same node overwrites the old pointer with freeing it;
                -- caller must hold the private lock;
        */
        bool _link_node(treeNode<typeG>** dstNodeP, uint8_t* linkPathP, size_t length_linkedPath)
        {
            if (dstNodeP == nullptr) {return false; }
            if (linkPathP == nullptr && length_linkedPath != 0) {return false; }
            //std::unique_lock<std::mutex> lock(privateMutex);

            treeNode<typeG> *dstNode = *dstNodeP;
            if (dstNode == nullptr) {return false; }

            uint8_t *linkPath_copy = nullptr;
            if (length_linkedPath != 0)
            {
                linkPath_copy = new (std::nothrow) uint8_t[length_linkedPath];
                if (linkPath_copy == nullptr) {return false; }
                std::memcpy(linkPath_copy, linkPathP, length_linkedPath);
            }

            treeNode<typeG> **linkNodeP = _find_node(linkPath_copy, length_linkedPath);
            if (linkNodeP == nullptr || *linkNodeP == nullptr)
            {
                delete[] linkPath_copy;
                return false;
            }

            delete[] dstNode->externalRelatedLink_nodePathP;
            dstNode->externalRelatedLink_nodePathP = linkPath_copy;
            dstNode->length_externaRelatedlLink_nodePath = length_linkedPath;
            dstNode->has_externalLink = true;

            _forget_link_cache(dstNode);
            dstNode->linkNode_hitCounts = 0;
            return true;
        }


    public:
        //std::mutex publicMutex;

        Tree256(size_t HVcache_size=HVcache_minSize)
         : rootNodeP(nullptr),
         size0f_HVcache(std::max(HVcache_minSize, HVcache_size)),
         HVcache_nodeP(nullptr),
         HVcache_aliveState(nullptr),
         HVcache_generation(nullptr)
        {
            // Build everything under unique_ptr first: if one allocation throws,
            // the already-allocated blocks are released even though ~Tree256 is not called.
            std::unique_ptr<treeNode<typeG>> root(new treeNode<typeG>());
            std::unique_ptr<treeNode<typeG> *[]> nodeCache(new treeNode<typeG> *[size0f_HVcache +1]());
            std::unique_ptr<bool[]> aliveState(new bool[size0f_HVcache +1]());
            std::unique_ptr<uint32_t[]> generation(new uint32_t[size0f_HVcache +1]());

            rootNodeP = root.release();
            HVcache_nodeP = nodeCache.release();
            HVcache_aliveState = aliveState.release();
            HVcache_generation = generation.release();
        }

        Tree256(const Tree256 &) = delete;
        Tree256 &operator=(const Tree256 &) = delete;

        
        /*
            input-- path of node(must be exist), length of nodePath
            output-- true if success, false if not
            do what-- create a children-node-pointer array with nullptr-filled and mount it to the node
            tips-- if the node already points to a children-node-pointer array, it will do nothing and return true;
                -- the public API of the private func _create_childrenArray();
                -- hold private lock;
        */
        bool createChildren(uint8_t* nodePathP, size_t length)
        {
            std::unique_lock<std::mutex> lock(privateMutex);
            treeNode<typeG> **nodeP = _find_node(nodePathP, length);
            return _create_childrenArray(nodeP);
        }

        /*
            input-- path of node, length of nodePath, the global pointer of data
            output-- true if success, false if args invalid or mount failed
            do what-- ensure the path exists, then write dataGP into the target node
            tips-- dataGP is a non-owning pointer; the tree does not delete old/new data;
                -- the data must be maintained by the owner;
                -- a later put_data on the same path overwrites the old pointer without freeing it;
                -- hold private lock;
        */
        bool put_data(uint8_t* nodePathP, size_t length, typeG* dataGP)
        {
            if (dataGP == nullptr)  {return false; }
            std::unique_lock<std::mutex> lock(privateMutex);

            treeNode<typeG> **targetNodeP = _mount_node(nodePathP, length);
            if (targetNodeP == nullptr) {return false; }

            (*targetNodeP)->dataP = dataGP;
            ++visited_timesCount;
            return true;
        }

        /*
            input-- path of node, length of nodePath
            output-- return the pointer of the data if it exists, or nullptr
            do what-- get the data of the node if it exists
            tips-- if the length == 0, return the data of the root node;
                -- hold private lock;
        */
        typeG* get_data(uint8_t* nodePathP, size_t length)
        {
            if (nodePathP == nullptr && length != 0) {return nullptr; }
            std::unique_lock<std::mutex> lock(privateMutex);

            treeNode<typeG> **targetNodeP = _find_node(nodePathP, length);
            if (targetNodeP == nullptr) {return nullptr; }
            ++visited_timesCount;
            return (*targetNodeP)->dataP;
        }

        /*
            input-- path of node, length of nodePath
            output-- true if success, false if args invalid or erase failed
            do what-- erase the data of the node if it exists
            tips-- if the length == 0, erase the data of the root node;
                -- hold private lock;
        */
        bool erase_data(uint8_t* nodePathP, size_t length)
        {
            if (nodePathP == nullptr && length != 0) {return false; }
            std::unique_lock<std::mutex> lock(privateMutex);

            treeNode<typeG> **targetNodeP = _find_node(nodePathP, length);
            if (targetNodeP == nullptr) {return false; }

            (*targetNodeP)->dataP = nullptr;
            ++visited_timesCount;
            return true;
        }

        /*
            input-- path of node, length of nodePath, path of node to linked, length of link_nodePath
            output-- true if success, false if args invalid or link failed
            do what-- link a node to another node
            tips-- the dst and src nodes must exist;
                -- a later link_node on the same node overwrites the old pointer with freeing it;
                -- hold private lock;
        */
        bool link_node(uint8_t* dst_nodePathP, size_t from_dstNodePath, uint8_t* linkPathP, size_t length_linkedPath)
        {
            std::unique_lock<std::mutex> lock(privateMutex);
            treeNode<typeG> **dstNodeP = _find_node(dst_nodePathP, from_dstNodePath);
            if (dstNodeP == nullptr) {return false; }
            return _link_node(dstNodeP, linkPathP, length_linkedPath);
        }

        /*
            input-- the pointer pointed to a node
            output-- a pointer pointes to a node if it exists, or nullptr
            do what-- return the pointer of the node linked by the node from input if it exists and args valid
            tips-- find from cache at first when the node from input says the link node is in cache;
                -- a cache hit requires a live slot with the same generation recorded by the source node;
                -- if the slot was cleared, evicted or reused, clear the source record and find it by path;
                -- hold private lock;
        */
        treeNode<typeG>** get_linkNode(treeNode<typeG>** nodeP)
        {
            if (nodeP == nullptr) {return nullptr; }
            std::unique_lock<std::mutex> lock(privateMutex);

            treeNode<typeG> *node = *nodeP;
            if (node == nullptr) {return nullptr; }
            if (!node->has_externalLink) {return nullptr; }
            if (node->externalRelatedLink_nodePathP == nullptr && node->length_externaRelatedlLink_nodePath != 0) {return nullptr; }
            if (node->length_externaRelatedlLink_nodePath == 0) {return &rootNodeP; }

            treeNode<typeG> **resNodeP = nullptr;
            if (node->is_linkNode_targetCached)
            {
                const uint32_t cacheIndex = node->linkNode_cacheIndex;
                bool cacheHit = false;
                if (cacheIndex != 0 && cacheIndex <= size0f_HVcache
                    && HVcache_aliveState[cacheIndex]
                    && HVcache_nodeP[cacheIndex] != nullptr
                    && HVcache_generation[cacheIndex] == node->linkNode_cacheGeneration)
                {
                    treeNode<typeG> *cachedNode = HVcache_nodeP[cacheIndex];
                    if (cachedNode->self_in_cache && cachedNode->self_cacheIndex == cacheIndex)
                    {
                        resNodeP = HVcache_nodeP + cacheIndex;
                        cacheHit = true;
                    }
                }
                if (cacheHit)
                {
                    ++node->linkNode_hitCounts;
                    return resNodeP;
                }
                // Slot was cleared, evicted or reused; the path is the source of truth.
                _forget_link_cache(node);
            }

            resNodeP = _find_node(node->externalRelatedLink_nodePathP, node->length_externaRelatedlLink_nodePath);
            if (resNodeP == nullptr || *resNodeP == nullptr) {return nullptr; }
            
            ++node->linkNode_hitCounts;
            if (node->linkNode_hitCounts > minCounts_move2cache) {_put2cache(nodeP); }
            return resNodeP;
        }

        /*
            input-- path of node, length of nodePath
            output-- true if success, false if args invalid or free failed
            do what-- free the node tree
            tips-- if a node has no children-node-pointer array, will return true;
                -- release the node, its external path and its own cache slot; does not release datas;
                -- cant release from the root node;
                -- hold private lock;
        */
        bool free_nodeTree(uint8_t* nodePathP, size_t length)
        {
            std::unique_lock<std::mutex> lock(privateMutex);
            treeNode<typeG> **targetNodeP = _find_node(nodePathP, length);
            if (targetNodeP == nullptr || targetNodeP == &rootNodeP) {return false; }
            return _free_nodeTree(targetNodeP);
        }

        /*
            tips-- hold private lock;
                -- if the layer is too deep, it's recommended to manually release it in segments starting from the end;
        */
        ~Tree256()
        {
            std::unique_lock<std::mutex> lock(privateMutex);
            _free_nodeTree(&rootNodeP);
            //delete rootNodeP;
            delete[] HVcache_nodeP;
            delete[] HVcache_aliveState;
            delete[] HVcache_generation;
        }

    };


};




