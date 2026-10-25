#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>



namespace CKtree
{

    static const size_t children_size = 256;
    static const size_t HVcache_minSize = 0xff;
    static const uint32_t minCounts_move2cache = 0x0f; // just can when tha bigger than it
    static const uint32_t deadNum_32u = 0xdeadc01d;


    /*
        the basic node of the tree;
        the status of the linkNode will be maintained at the next visit runtime;
    */
    struct treeNode
    {
        void *dataP = nullptr; // data is from the user, so must be freed by the user and the type ofit is user's responsibility
        treeNode **childFP = nullptr;
        uint8_t *externalRelatedLink_nodePathP = nullptr;
        bool is_linkNode_targetCached = false; // for the externalRelatedLink_node
        bool self_in_cache = false;
        uint32_t linkNode_cacheIndex = 0; // index ==0 -> nullptr(cached not), for the externalRelatedLink_node, too 
        uint32_t linkNode_hitCounts = 0; // count the hit(jumped) times for the externalRelatedLink_node
        uint32_t self_cacheIndex = 0;
        size_t length_externaRelatedlLink_nodePath = 0; // nomal length of array
    };

    struct resData_32u
    {
        uint32_t value32u = deadNum_32u;
        bool is_valid = false;
    };
    struct resData_sizet
    {
        size_t value = 0;
        bool is_valid = false;
    };


    class Tree256
    {
    private:
        treeNode *rootNodeP;
        uint64_t visited_timesCount = 0; // just count for data I/O
        size_t size0f_HVcache;
        treeNode **HVcache_nodeP; // length -> size +1
        bool *HVcache_aliveState;

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
        resData_32u _can_putInto_cache(treeNode** nodeP=nullptr)
        {
            resData_32u res;
            if (nodeP == nullptr || *nodeP == nullptr) {return res; } // cant
            for (size_t index =1; index <= size0f_HVcache; ++index)
            {
                if (*nodeP == HVcache_nodeP[index]) // check if it's already in the cache
                {
                    res.value32u = static_cast<uint32_t>(index);
                    res.is_valid = true;
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
                HVcache_nodeP[res.value32u] = *nodeP;
                HVcache_aliveState[res.value32u] = true;
                (*nodeP)->self_in_cache = true;
                (*nodeP)->self_cacheIndex = res.value32u;
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
        bool _remove_from_cache(treeNode** nodeP)
        {
            const uint32_t cacheIndex = (*nodeP)->self_cacheIndex;
            if (cacheIndex == 0 || HVcache_nodeP[cacheIndex] != (*nodeP) || cacheIndex > size0f_HVcache
                 || HVcache_aliveState[cacheIndex] == false) {return false; }
            HVcache_aliveState[cacheIndex] = false;
            HVcache_nodeP[cacheIndex] = nullptr;
            (*nodeP)->self_in_cache = false;
            (*nodeP)->self_cacheIndex = 0;
            return true;
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
         
        /*
            input-- path of node, length of nodePath
            output-- a pointer points to the node
            do what-- return the pointer points to the node if it exists, or nullptr
            tips-- nullptr path with length != 0 is invalid;
                -- caller must hold the private lock;
        */
        treeNode** _find_node(uint8_t* nodePathP, size_t length)
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

        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- create a children-node-pointer array with nullptr-filled and mount it to the node
            tips-- if the node already points to a children-node-pointer array, it will do nothing and return true;
                -- caller must hold the private lock;
        */
        bool _create_childrenArray(treeNode** nodeP)
        {
            if (nodeP == nullptr || *nodeP == nullptr) {return false; }
            if ((*nodeP)->childFP != nullptr) {return true; }
            (*nodeP)->childFP = new treeNode *[children_size]();
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
        treeNode** _mount_node(uint8_t* nodePathP, size_t length)
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
        
        /*
            input-- a pointer pointed to a node
            output-- true if success, false if not
            do what-- recursively release the entire node tree rooted at this node
            tips-- if a node has no children-node-pointer array, will return true;
                -- just release the node, no externalRelatedLink_nodes(nodes, not path) or datas;
                -- caller must hold the private lock;
        */
        bool _free_nodeTree(treeNode** nodePP)
        {
            if (nodePP == nullptr || *nodePP == nullptr) {return false; }
            treeNode *nodeP = *nodePP;
            *nodePP = nullptr;

            _remove_from_cache(&nodeP);
            /*const uint32_t cacheIndex = nodeP->self_cacheIndex;
            if (nodeP->self_in_cache && cacheIndex != 0 && cacheIndex <= size0f_HVcache
                 && HVcache_nodeP[cacheIndex] == nodeP && HVcache_aliveState[cacheIndex]) 
            {
                HVcache_aliveState[cacheIndex] = false;
                HVcache_nodeP[cacheIndex] = nullptr;
                nodeP->self_in_cache = false;
                nodeP->self_cacheIndex = 0;
            }//*/

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

        /*
            input-- path of node, length of nodePath
            output-- true if success, false if not
            do what-- try to put the node linked by the specified node into the cache
            tips-- nodePath -> nodeP -> externalRelatedLink_node as targetNode -> put it into the cache;
                -- if the targetNode is already in the cache, return false;
                -- hold private lock;
        */
        bool _put2cache(uint8_t* nodePathP, size_t length)
        {
            std::unique_lock<std::mutex> lock(privateMutex);
            treeNode **nodeP = _find_node(nodePathP, length);
            if (nodeP == nullptr || *nodeP == nullptr) {return false; }
            if ((*nodeP)->externalRelatedLink_nodePathP == nullptr
                 || (*nodeP)->length_externaRelatedlLink_nodePath == 0) {return false; }

            treeNode **targetNodeP = _find_node((*nodeP)->externalRelatedLink_nodePathP, (*nodeP)->length_externaRelatedlLink_nodePath);
            if (targetNodeP == nullptr || *targetNodeP == nullptr) {return false; }

            // Only trust linkNode_cacheIndex while its slot still points at this target.
            const uint32_t oldIndex = (*nodeP)->linkNode_cacheIndex;
            if (oldIndex != 0 && oldIndex <= size0f_HVcache && HVcache_aliveState[oldIndex]
                && HVcache_nodeP[oldIndex] == *targetNodeP)
            {
                (*nodeP)->is_linkNode_targetCached = true;
                return false; // target is already cached
            }
            (*nodeP)->linkNode_cacheIndex = 0;
            (*nodeP)->is_linkNode_targetCached = false;

            resData_32u res = _can_putInto_cache(targetNodeP);
            if (res.is_valid) // can put it in
            {
                (*nodeP)->is_linkNode_targetCached = true;
                (*nodeP)->linkNode_cacheIndex = res.value32u;
                return true;
            }
            return false;
        }

        /*
            input-- the index of the cache slot
            output-- a pointer points to the node
            do what-- return the pointer of the node if it exists, or nullptr
        */
        treeNode** _find_from_cache(size_t cacheIndex)
        {
            if (cacheIndex == 0 || cacheIndex > size0f_HVcache) {return nullptr; }
            if (HVcache_aliveState[cacheIndex] == false && HVcache_nodeP[cacheIndex] == nullptr) {return nullptr; }

            return HVcache_nodeP + cacheIndex;
        }

        /*
            input-- a pointer pointed to a node
            output-- the index of the cache slot
            do what-- return the index of the cache slot if it exists, or return 0
        */
        size_t _find_from_cache(treeNode** nodeP)
        {
            if (nodeP == nullptr || *nodeP == nullptr) {return 0; }
            for (size_t index =1; index <= size0f_HVcache; ++index)
            {
                if (HVcache_aliveState[index] && HVcache_nodeP[index] == *nodeP) {return index; }
            }
            return 0;
        }


    public:
        //std::mutex publicMutex;

        Tree256(size_t HVcache_size=HVcache_minSize)
         : rootNodeP(new treeNode()),
         size0f_HVcache(std::max(HVcache_minSize, HVcache_size)),
         HVcache_nodeP(new treeNode *[size0f_HVcache +1]()),
         HVcache_aliveState(new bool[size0f_HVcache +1]())
        {}

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
            treeNode **nodeP = _find_node(nodePathP, length);
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
        bool put_data(uint8_t* nodePathP, size_t length, void* dataGP)
        {
            if (dataGP == nullptr)  {return false; }
            std::unique_lock<std::mutex> lock(privateMutex);

            treeNode **targetNodeP = _mount_node(nodePathP, length);
            if (targetNodeP == nullptr) {return false; }

            (*targetNodeP)->dataP = dataGP;
            return true;
        }

        /*
            input-- path of node, length of nodePath
            output-- return the pointer of the data if it exists, or nullptr
            do what-- get the data of the node if it exists
            tips-- if the length == 0, return the data of the root node;
                -- hold private lock;
        */
        void* get_data(uint8_t* nodePathP, size_t length)
        {
            if (nodePathP == nullptr && length != 0) {return nullptr; }
            std::unique_lock<std::mutex> lock(privateMutex);

            treeNode **targetNodeP = _find_node(nodePathP, length);
            if (targetNodeP == nullptr) {return nullptr; }
            return (*targetNodeP)->dataP;
        }

        /* pause */
        bool free_node(uint8_t* /*nodePathP*/, size_t /*length*/)
        {return false; }

        ~Tree256()
        {
            std::unique_lock<std::mutex> lock(privateMutex);
            _free_nodeTree(&rootNodeP);
            //delete rootNodeP;
            delete[] HVcache_nodeP;
            delete[] HVcache_aliveState;
        }

    };


};




