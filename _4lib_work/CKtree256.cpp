#include "CKtree256.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

/*
 * Library-format implementation split from classBasic.cpp.
 * Source SHA256: ad23e0e0e176dcb0580285fd33082937aa5d46cedfa65f56b29a3dc4ca874bc6.
 * Generated on 2026-11-16. Regenerate this file when the development source changes.
 */

namespace CKtree
{
    static const size_t children_size = 256;
    static const uint32_t minCounts_move2cache = 0x0f; // just can while tha bigger than it
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
    struct treeNode
    {
        void *dataP = nullptr; // data is from the user, so must be released by the user and the type ofit is user's responsibility
        treeNode **childFP = nullptr;
        uint8_t *externalRelatedLink_nodePathP = nullptr;
        bool has_externalLink = false;
        bool is_linkNode_targetCached = false; // for the externalRelatedLink_node
        bool self_in_cache = false;
        uint32_t linkNode_cacheIndex = 0; // index ==0 -> nullptr(cached not), for the externalRelatedLink_node, too
        uint32_t linkNode_cacheGeneration = 0; // cache-slot generation this index referred to
        uint32_t hitCounts_4beJumped = 0; // how many times this node was hit as a jump target
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

    Tree256::Tree256(size_t HVcache_size)
         : rootNodeP(nullptr),
         size0f_HVcache(std::max(HVcache_minSize, HVcache_size)),
         HVcache_nodeP(nullptr),
         HVcache_aliveState(nullptr),
         HVcache_generation(nullptr)
    {
        // Build everything under unique_ptr first: if one allocation throws,
        // the already-allocated blocks are released even though ~Tree256 is not called.
        std::unique_ptr<treeNode> root(new treeNode());
        std::unique_ptr<treeNode *[]> nodeCache(new treeNode *[size0f_HVcache +1]());
        std::unique_ptr<bool[]> aliveState(new bool[size0f_HVcache +1]());
        std::unique_ptr<uint32_t[]> generation(new uint32_t[size0f_HVcache +1]());

        rootNodeP = root.release();
        HVcache_nodeP = nodeCache.release();
        HVcache_aliveState = aliveState.release();
        HVcache_generation = generation.release();
    }

    resData_32u Tree256::_can_putInto_cache(treeNode** nodeP)
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

    bool Tree256::_remove_self_from_cache(treeNode* node)
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

    bool Tree256::_forget_link_cache(treeNode* node)
    {
        if (node == nullptr) {return false; }
        const bool hadRecord = node->is_linkNode_targetCached || node->linkNode_cacheIndex != 0;
        node->is_linkNode_targetCached = false;
        node->linkNode_cacheIndex = 0;
        node->linkNode_cacheGeneration = 0;
        return hadRecord;
    }

    bool Tree256::_node_isExist(uint8_t* nodePathP, size_t length)
    {
        treeNode **nodeTP = &rootNodeP;
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

    treeNode** Tree256::_find_node(uint8_t* nodePathP, size_t length)
    {
        if (length == 0) {return &rootNodeP; }
        tempNodeP_4easyFind = nullptr;
        if (!_node_isExist(nodePathP, length)) {return nullptr; }
        treeNode **nodeTP = &rootNodeP;
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

    bool Tree256::_create_childrenArray(treeNode** nodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) {return false; }
        if ((*nodeP)->childFP != nullptr) {return true; }
        (*nodeP)->childFP = new treeNode *[children_size]();
        return true;
    }

    treeNode** Tree256::_mount_node(uint8_t* nodePathP, size_t length)
    {
        if (nodePathP == nullptr && length != 0) {return nullptr; }
        treeNode **nodeLP = &rootNodeP;
        if (*nodeLP == nullptr) {return nullptr; }

        for (size_t index =0; index < length; ++index)
        {
            if (!_create_childrenArray(nodeLP)) {return nullptr; }

            treeNode **nodeTP = &((*nodeLP)->childFP[nodePathP[index]]);
            if (*nodeTP == nullptr) {*nodeTP = new treeNode(); }

            nodeLP = nodeTP;
        }
        return nodeLP;
    }

    bool Tree256::_free_nodeTree(treeNode** nodePP)
    {
        if (nodePP == nullptr || *nodePP == nullptr) {return false; }
        treeNode *nodeP = *nodePP;
        *nodePP = nullptr;

        _remove_self_from_cache(nodeP);

        if (treeNode **children = nodeP->childFP)
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

    bool Tree256::_put2cache(uint8_t* nodePathP, size_t length)
    {
        treeNode **nodeP = _find_node(nodePathP, length);
        return _put2cache(nodeP);
    }

    bool Tree256::_put2cache(treeNode** nodeP, treeNode** targetNodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) {return false; }
        //std::unique_lock<std::mutex> lock(privateMutex);
        if ((*nodeP)->externalRelatedLink_nodePathP == nullptr
             || (*nodeP)->length_externaRelatedlLink_nodePath == 0) {return false; }

        if (targetNodeP == nullptr)
        {
            targetNodeP = _find_node((*nodeP)->externalRelatedLink_nodePathP, (*nodeP)->length_externaRelatedlLink_nodePath);
        }
        if (targetNodeP == nullptr || *targetNodeP == nullptr) {return false; }
        if ((*targetNodeP)->hitCounts_4beJumped <= minCounts_move2cache) {return false; }

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

    treeNode** Tree256::_find_from_cache(size_t cacheIndex)
    {
        if (cacheIndex == 0 || cacheIndex > size0f_HVcache) {return nullptr; }
        if (HVcache_aliveState[cacheIndex] == false || HVcache_nodeP[cacheIndex] == nullptr) {return nullptr; }

        return HVcache_nodeP + cacheIndex;
    }

    size_t Tree256::_find_from_cache(treeNode** nodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) {return 0; }
        for (size_t index =1; index <= size0f_HVcache; ++index)
        {
            if (HVcache_aliveState[index] && HVcache_nodeP[index] == *nodeP) {return index; }
        }
        return 0;
    }

    bool Tree256::_link_node(treeNode** dstNodeP, uint8_t* linkPathP, size_t length_linkedPath)
    {
        if (dstNodeP == nullptr) {return false; }
        if (linkPathP == nullptr && length_linkedPath != 0) {return false; }
        //std::unique_lock<std::mutex> lock(privateMutex);

        treeNode *dstNode = *dstNodeP;
        if (dstNode == nullptr) {return false; }

        uint8_t *linkPath_copy = nullptr;
        if (length_linkedPath != 0)
        {
            linkPath_copy = new uint8_t[length_linkedPath]; // may std::bad_alloc
            std::memcpy(linkPath_copy, linkPathP, length_linkedPath);
        }

        treeNode **linkNodeP = _find_node(linkPath_copy, length_linkedPath);
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
        return true;
    }

    treeNode* Tree256::_get_linkNode(treeNode** nodeP)
    {
        if (nodeP == nullptr) {return nullptr; }
        //std::unique_lock<std::mutex> lock(privateMutex);

        treeNode *node = *nodeP;
        if (node == nullptr) {return nullptr; }
        if (!node->has_externalLink) {return nullptr; }
        if (node->externalRelatedLink_nodePathP == nullptr && node->length_externaRelatedlLink_nodePath != 0) {return nullptr; }
        if (node->length_externaRelatedlLink_nodePath == 0)
        {
            if (rootNodeP->hitCounts_4beJumped != UINT32_MAX) {++rootNodeP->hitCounts_4beJumped; }
            return rootNodeP;
        }

        treeNode **resNodeP = nullptr;
        if (node->is_linkNode_targetCached)
        {
            const uint32_t cacheIndex = node->linkNode_cacheIndex;
            bool cacheHit = false;
            if (cacheIndex != 0 && cacheIndex <= size0f_HVcache
                && HVcache_aliveState[cacheIndex]
                && HVcache_nodeP[cacheIndex] != nullptr
                && HVcache_generation[cacheIndex] == node->linkNode_cacheGeneration)
            {
                treeNode *cachedNode = HVcache_nodeP[cacheIndex];
                if (cachedNode->self_in_cache && cachedNode->self_cacheIndex == cacheIndex)
                {
                    resNodeP = HVcache_nodeP + cacheIndex;
                    cacheHit = true;
                }
            }
            if (cacheHit)
            {
                treeNode *cacheNode = *resNodeP;
                if (cacheNode->hitCounts_4beJumped != UINT32_MAX) {++cacheNode->hitCounts_4beJumped; }
                return cacheNode;
            }
            // Slot was cleared, evicted or reused; the path is the source of truth.
            _forget_link_cache(node);
        }

        resNodeP = _find_node(node->externalRelatedLink_nodePathP, node->length_externaRelatedlLink_nodePath);
        if (resNodeP == nullptr || *resNodeP == nullptr) {return nullptr; }

        treeNode *targetNode = *resNodeP;
        if (targetNode->hitCounts_4beJumped != UINT32_MAX) {++targetNode->hitCounts_4beJumped; }
        if (targetNode->hitCounts_4beJumped > minCounts_move2cache) {_put2cache(nodeP, resNodeP); }
        return targetNode;
    }

    bool Tree256::_cache_eviction(bool sweep, bool hit_downrade, uint8_t penalty)
    {
        bool result = false, isFull = true;
        treeNode *leastNode = nullptr;
        uint32_t leastCount = UINT32_MAX;
        for (size_t index =1; index <= size0f_HVcache; ++index)
        {
            if (!HVcache_aliveState[index])
            {
                HVcache_nodeP[index] = nullptr;
                isFull = false;
                continue;
            }
            if (HVcache_nodeP[index] == nullptr)
            {
                HVcache_aliveState[index] = false;
                isFull = false;
                continue;
            }

            // the node is valid
            treeNode *node = HVcache_nodeP[index];
            if (hit_downrade) {node->hitCounts_4beJumped = node->hitCounts_4beJumped == 0 ? 0 : node->hitCounts_4beJumped - 1; }
            if (node->hitCounts_4beJumped <= minCounts_move2cache && sweep)
            {
                bool resBT = _remove_self_from_cache(node);
                if (!resBT) {continue; }
                node->hitCounts_4beJumped = (node->hitCounts_4beJumped <= penalty) ? 0 : node->hitCounts_4beJumped - penalty;
                result = true;
                continue;
            }

            // the node had not be evicted
            if (leastNode == nullptr)
            {
                leastCount = node->hitCounts_4beJumped;
                leastNode = node;
            }
            else if (node->hitCounts_4beJumped < leastCount)
            {
                leastCount = node->hitCounts_4beJumped;
                leastNode = node;
            }
        }
        if (isFull && !result && leastNode != nullptr)
        {
            bool resBT = _remove_self_from_cache(leastNode);
            if (!resBT) {return false; }
            leastNode->hitCounts_4beJumped = (leastNode->hitCounts_4beJumped <= penalty) ? 0 : leastNode->hitCounts_4beJumped - penalty;
            result = true;
        }
        return result;
    }

    bool Tree256::createChildren(uint8_t* nodePathP, size_t length)
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        treeNode **nodeP = _find_node(nodePathP, length);
        return _create_childrenArray(nodeP);
    }

    bool Tree256::put_data(uint8_t* nodePathP, size_t length, void* dataGP)
    {
        if (dataGP == nullptr)  {return false; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode **targetNodeP = _mount_node(nodePathP, length);
        if (targetNodeP == nullptr) {return false; }

        (*targetNodeP)->dataP = dataGP;
        ++visited_timesCount;
        return true;
    }

    void* Tree256::get_data(uint8_t* nodePathP, size_t length)
    {
        if (nodePathP == nullptr && length != 0) {return nullptr; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode **targetNodeP = _find_node(nodePathP, length);
        if (targetNodeP == nullptr) {return nullptr; }
        ++visited_timesCount;
        return (*targetNodeP)->dataP;
    }

    bool Tree256::erase_data(uint8_t* nodePathP, size_t length)
    {
        if (nodePathP == nullptr && length != 0) {return false; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode **targetNodeP = _find_node(nodePathP, length);
        if (targetNodeP == nullptr) {return false; }

        (*targetNodeP)->dataP = nullptr;
        ++visited_timesCount;
        return true;
    }

    bool Tree256::link_node(uint8_t* dst_nodePathP, size_t from_dstNodePath, uint8_t* linkPathP, size_t length_linkedPath)
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        treeNode** dstNodeP = _find_node(dst_nodePathP, from_dstNodePath);
        if (dstNodeP == nullptr) {return false; }
        return _link_node(dstNodeP, linkPathP, length_linkedPath);
    }

    treeNode* Tree256::get_linkNode(uint8_t* nodePathP, size_t length)
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        treeNode **targetNodeP = _find_node(nodePathP, length);
        return _get_linkNode(targetNodeP);
    }

    bool Tree256::free_nodeTree(uint8_t* nodePathP, size_t length)
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        treeNode **targetNodeP = _find_node(nodePathP, length);
        if (targetNodeP == nullptr || targetNodeP == &rootNodeP) {return false; }
        return _free_nodeTree(targetNodeP);
    }

    Tree256::~Tree256()
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        _free_nodeTree(&rootNodeP);
        //delete rootNodeP;
        delete[] HVcache_nodeP;
        delete[] HVcache_aliveState;
        delete[] HVcache_generation;
    }

}
