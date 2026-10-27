#include "CKtree256.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace CKtree
{
    namespace
    {
        constexpr std::size_t children_size = 256;
        constexpr std::uint32_t minCounts_move2cache = 0x0f;
        constexpr std::uint32_t deadNum_32u = 0xdeadc01d;
    }

    struct resData_32u
    {
        std::uint32_t value32u = deadNum_32u;
        bool is_valid = false;
    };

    struct resData_sizet
    {
        std::size_t value = 0;
        bool is_valid = false;
    };

    /*
     * Internal node. Public users only see the forward declaration in the
     * header; the layout stays private to this translation unit.
     */
    struct treeNode
    {
        void *dataP = nullptr; // user-owned / 调用方所有
        treeNode **childFP = nullptr;
        std::uint8_t *externalRelatedLink_nodePathP = nullptr;
        bool has_externalLink = false;
        bool is_linkNode_targetCached = false;
        bool self_in_cache = false;
        std::uint32_t linkNode_cacheIndex = 0;
        std::uint32_t linkNode_hitCounts = 0;
        std::uint32_t self_cacheIndex = 0;
        std::size_t length_externaRelatedlLink_nodePath = 0;
    };

    Tree256::Tree256(std::size_t HVcache_size)
        : rootNodeP(new treeNode()),
          visited_timesCount(0),
          size0f_HVcache(std::max(HVcache_minSize, HVcache_size)),
          HVcache_nodeP(new treeNode *[size0f_HVcache + 1]()),
          HVcache_aliveState(new bool[size0f_HVcache + 1]()),
          tempNodeP_4easyFind(nullptr)
    {
    }

    resData_32u Tree256::_can_putInto_cache(treeNode **nodeP)
    {
        resData_32u res;
        if (nodeP == nullptr || *nodeP == nullptr) { return res; }

        for (std::size_t index = 1; index <= size0f_HVcache; ++index)
        {
            if (*nodeP == HVcache_nodeP[index])
            {
                res.value32u = static_cast<std::uint32_t>(index);
                res.is_valid = true;
                break;
            }
            else if (HVcache_aliveState[index] == false && res.is_valid == false)
            {
                res.value32u = static_cast<std::uint32_t>(index);
                res.is_valid = true;
            }
        }

        if (res.is_valid)
        {
            HVcache_nodeP[res.value32u] = *nodeP;
            HVcache_aliveState[res.value32u] = true;
            (*nodeP)->self_in_cache = true;
            (*nodeP)->self_cacheIndex = res.value32u;
        }
        return res;
    }

    bool Tree256::_remove_from_cache(treeNode **nodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) { return false; }

        treeNode *node = *nodeP;
        const std::uint32_t cacheIndex = node->self_cacheIndex;
        if (cacheIndex == 0 || cacheIndex > size0f_HVcache) { return false; }
        if (HVcache_nodeP[cacheIndex] != node || HVcache_aliveState[cacheIndex] == false) { return false; }

        HVcache_aliveState[cacheIndex] = false;
        HVcache_nodeP[cacheIndex] = nullptr;
        node->self_in_cache = false;
        node->self_cacheIndex = 0;
        return true;
    }

    bool Tree256::_node_isExist(std::uint8_t *nodePathP, std::size_t length)
    {
        treeNode **nodeTP = &rootNodeP;
        if (nodePathP == nullptr) { return false; }
        if (rootNodeP == nullptr) { return false; }

        for (std::size_t index = 0; index < length; ++index)
        {
            if ((*nodeTP)->childFP == nullptr) { return false; }
            const std::uint8_t nodeIndex = nodePathP[index];
            if ((*nodeTP)->childFP[nodeIndex] == nullptr) { return false; }
            nodeTP = &((*nodeTP)->childFP[nodeIndex]);
        }

        tempNodeP_4easyFind = nodeTP;
        return true;
    }

    treeNode **Tree256::_find_node(std::uint8_t *nodePathP, std::size_t length)
    {
        if (length == 0) { return &rootNodeP; }
        tempNodeP_4easyFind = nullptr;
        if (!_node_isExist(nodePathP, length)) { return nullptr; }

        treeNode **nodeTP = &rootNodeP;
        if (tempNodeP_4easyFind != nullptr)
        {
            nodeTP = tempNodeP_4easyFind;
            tempNodeP_4easyFind = nullptr;
        }
        else
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                if ((*nodeTP)->childFP == nullptr) { return nullptr; }
                const std::uint8_t nodeIndex = nodePathP[index];
                if ((*nodeTP)->childFP[nodeIndex] == nullptr) { return nullptr; }
                nodeTP = &((*nodeTP)->childFP[nodeIndex]);
            }
        }
        return nodeTP;
    }

    bool Tree256::_create_childrenArray(treeNode **nodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) { return false; }
        if ((*nodeP)->childFP != nullptr) { return true; }
        (*nodeP)->childFP = new treeNode *[children_size]();
        return true;
    }

    treeNode **Tree256::_mount_node(std::uint8_t *nodePathP, std::size_t length)
    {
        if (nodePathP == nullptr && length != 0) { return nullptr; }
        treeNode **nodeLP = &rootNodeP;
        if (*nodeLP == nullptr) { return nullptr; }

        for (std::size_t index = 0; index < length; ++index)
        {
            if (!_create_childrenArray(nodeLP)) { return nullptr; }

            treeNode **nodeTP = &((*nodeLP)->childFP[nodePathP[index]]);
            if (*nodeTP == nullptr) { *nodeTP = new treeNode(); }

            nodeLP = nodeTP;
        }
        return nodeLP;
    }

    bool Tree256::_free_nodeTree(treeNode **nodePP)
    {
        if (nodePP == nullptr || *nodePP == nullptr) { return false; }

        treeNode *nodeP = *nodePP;
        *nodePP = nullptr;

        _remove_from_cache(&nodeP);

        if (treeNode **children = nodeP->childFP)
        {
            for (std::size_t index = 0; index < children_size; ++index)
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

    bool Tree256::_put2cache(std::uint8_t *nodePathP, std::size_t length)
    {
        treeNode **nodeP = _find_node(nodePathP, length);
        if (nodeP == nullptr || *nodeP == nullptr) { return false; }
        if ((*nodeP)->externalRelatedLink_nodePathP == nullptr
            || (*nodeP)->length_externaRelatedlLink_nodePath == 0) { return false; }
        if ((*nodeP)->linkNode_hitCounts <= minCounts_move2cache) { return false; }

        treeNode **targetNodeP = _find_node((*nodeP)->externalRelatedLink_nodePathP,
                                            (*nodeP)->length_externaRelatedlLink_nodePath);
        if (targetNodeP == nullptr || *targetNodeP == nullptr) { return false; }

        const std::uint32_t oldIndex = (*nodeP)->linkNode_cacheIndex;
        if (oldIndex != 0 && oldIndex <= size0f_HVcache && HVcache_aliveState[oldIndex]
            && HVcache_nodeP[oldIndex] == *targetNodeP)
        {
            (*nodeP)->is_linkNode_targetCached = true;
            return false;
        }
        (*nodeP)->linkNode_cacheIndex = 0;
        (*nodeP)->is_linkNode_targetCached = false;

        resData_32u res = _can_putInto_cache(targetNodeP);
        if (res.is_valid)
        {
            (*nodeP)->is_linkNode_targetCached = true;
            (*nodeP)->linkNode_cacheIndex = res.value32u;
            return true;
        }
        return false;
    }

    bool Tree256::_put2cache(treeNode **nodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) { return false; }
        if ((*nodeP)->externalRelatedLink_nodePathP == nullptr
            || (*nodeP)->length_externaRelatedlLink_nodePath == 0) { return false; }
        if ((*nodeP)->linkNode_hitCounts <= minCounts_move2cache) { return false; }

        treeNode **targetNodeP = _find_node((*nodeP)->externalRelatedLink_nodePathP,
                                            (*nodeP)->length_externaRelatedlLink_nodePath);
        if (targetNodeP == nullptr || *targetNodeP == nullptr) { return false; }

        const std::uint32_t oldIndex = (*nodeP)->linkNode_cacheIndex;
        if (oldIndex != 0 && oldIndex <= size0f_HVcache && HVcache_aliveState[oldIndex]
            && HVcache_nodeP[oldIndex] == *targetNodeP)
        {
            (*nodeP)->is_linkNode_targetCached = true;
            return false;
        }
        (*nodeP)->linkNode_cacheIndex = 0;
        (*nodeP)->is_linkNode_targetCached = false;

        resData_32u res = _can_putInto_cache(targetNodeP);
        if (res.is_valid)
        {
            (*nodeP)->is_linkNode_targetCached = true;
            (*nodeP)->linkNode_cacheIndex = res.value32u;
            return true;
        }
        return false;
    }

    treeNode **Tree256::_find_from_cache(std::size_t cacheIndex)
    {
        if (cacheIndex == 0 || cacheIndex > size0f_HVcache) { return nullptr; }
        if (HVcache_aliveState[cacheIndex] == false || HVcache_nodeP[cacheIndex] == nullptr) { return nullptr; }

        return HVcache_nodeP + cacheIndex;
    }

    std::size_t Tree256::_find_from_cache(treeNode **nodeP)
    {
        if (nodeP == nullptr || *nodeP == nullptr) { return 0; }
        for (std::size_t index = 1; index <= size0f_HVcache; ++index)
        {
            if (HVcache_aliveState[index] && HVcache_nodeP[index] == *nodeP) { return index; }
        }
        return 0;
    }

    bool Tree256::createChildren(std::uint8_t *nodePathP, std::size_t length)
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        treeNode **nodeP = _find_node(nodePathP, length);
        return _create_childrenArray(nodeP);
    }

    bool Tree256::put_data(std::uint8_t *nodePathP, std::size_t length, void *dataGP)
    {
        if (dataGP == nullptr) { return false; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode **targetNodeP = _mount_node(nodePathP, length);
        if (targetNodeP == nullptr) { return false; }

        (*targetNodeP)->dataP = dataGP;
        return true;
    }

    void *Tree256::get_data(std::uint8_t *nodePathP, std::size_t length)
    {
        if (nodePathP == nullptr && length != 0) { return nullptr; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode **targetNodeP = _find_node(nodePathP, length);
        if (targetNodeP == nullptr) { return nullptr; }
        return (*targetNodeP)->dataP;
    }

    bool Tree256::erase_data(std::uint8_t *nodePathP, std::size_t length)
    {
        if (nodePathP == nullptr && length != 0) { return false; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode **targetNodeP = _find_node(nodePathP, length);
        if (targetNodeP == nullptr) { return false; }

        (*targetNodeP)->dataP = nullptr;
        return true;
    }

    bool Tree256::link_node(treeNode **dstNodeP, std::uint8_t *linkPathP, std::size_t length_linkedPath)
    {
        if (dstNodeP == nullptr) { return false; }
        if (linkPathP == nullptr && length_linkedPath != 0) { return false; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode *dstNode = *dstNodeP;
        if (dstNode == nullptr) { return false; }

        std::uint8_t *linkPath_copy = nullptr;
        if (length_linkedPath != 0)
        {
            linkPath_copy = new (std::nothrow) std::uint8_t[length_linkedPath];
            if (linkPath_copy == nullptr) { return false; }
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

        dstNode->is_linkNode_targetCached = false;
        dstNode->linkNode_cacheIndex = 0;
        dstNode->linkNode_hitCounts = 0;
        return true;
    }

    treeNode **Tree256::get_linkNode(treeNode **nodeP)
    {
        if (nodeP == nullptr) { return nullptr; }
        std::unique_lock<std::mutex> lock(privateMutex);

        treeNode *node = *nodeP;
        if (node == nullptr) { return nullptr; }
        if (!node->has_externalLink) { return nullptr; }
        if (node->externalRelatedLink_nodePathP == nullptr && node->length_externaRelatedlLink_nodePath != 0) { return nullptr; }
        if (node->length_externaRelatedlLink_nodePath == 0) { return &rootNodeP; }

        treeNode **resNodeP;
        if (node->is_linkNode_targetCached)
        {
            resNodeP = _find_from_cache(node->linkNode_cacheIndex);
            if (resNodeP != nullptr && *resNodeP != nullptr)
            {
                ++node->linkNode_hitCounts;
                return resNodeP;
            }
            node->linkNode_cacheIndex = 0;
            node->is_linkNode_targetCached = false;
        }

        resNodeP = _find_node(node->externalRelatedLink_nodePathP, node->length_externaRelatedlLink_nodePath);
        if (resNodeP == nullptr || *resNodeP == nullptr) { return nullptr; }

        ++node->linkNode_hitCounts;
        if (node->linkNode_hitCounts > minCounts_move2cache) { _put2cache(nodeP); }
        return resNodeP;
    }

    bool Tree256::free_node(std::uint8_t * /*nodePathP*/, std::size_t /*length*/, bool /*freeData*/)
    {
        return false;
    }

    Tree256::~Tree256()
    {
        std::unique_lock<std::mutex> lock(privateMutex);
        _free_nodeTree(&rootNodeP);
        delete[] HVcache_nodeP;
        delete[] HVcache_aliveState;
    }
}
