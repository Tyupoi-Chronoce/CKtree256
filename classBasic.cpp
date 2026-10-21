#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>



namespace CKtree
{

    static const size_t HVcache_minSize = 0xff;
    static const uint32_t minCounts_move2cache = 0x0f; // just can when tha bigger than it


    typedef struct treeNode
    {
        void *dataP = nullptr;
        treeNode **childFP = nullptr;
        uint8_t *externalRelatedLink_nodePathP = nullptr;
        bool is_targetCached = false;
        uint32_t index_4cache = 0; // index ==0 -> nullptr(cached not)
        uint32_t hit_count = 0; // count the hit times for the node it points to by nodePath
        size_t length_externaRelatedlLink_nodePath = 0; // the max valid index +1
    };


    class Tree256
    {
    private:
        treeNode *rootNodeP;
        uint64_t visited_timesCount = 0; // just count for data I/O
        size_t size0f_HVcache;
        treeNode **HVcache_nodeP; // length -> size +1
        bool *HVcache_aliveState;

        treeNode **tempNodeP = nullptr;


        bool _node_isExist(uint8_t* nodePathP, size_t length)
        {
            treeNode **nodeTP = &rootNodeP;
            for (size_t index =0; index < length; ++index)
            {
                if ((*nodeTP)->childFP == nullptr) {return false; }
                uint8_t nodeIndex = nodePathP[index];
                if ((*nodeTP)->childFP[nodeIndex] == nullptr) {return false; }
                nodeTP = &((*nodeTP)->childFP[nodeIndex]);
            }
            tempNodeP = nodeTP;
            return true;
        }
    
        /*
            input-- pointer pointed to a node
            output-- true if success, false if not
            do what-- recursively release the entire node tree rooted at this node
            tips-- if a node has no children-node-pointer array, will return true;
                -- just release the node, no externalLinks or datas

        */
        bool _free_nodeTree(treeNode** nodePP)
        {
            if (nodePP == nullptr || *nodePP == nullptr) {return false; }
            treeNode *nodeP = *nodePP;
            *nodePP = nullptr;

            if (treeNode **children = nodeP->childFP)
            {
                for (size_t index = 0; index < 256; ++index)   
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


    public:
        std::mutex Tree_Mutex;

        Tree256(size_t HVcache_size=HVcache_minSize)
         : rootNodeP(new treeNode()),
         size0f_HVcache(std::max(HVcache_minSize, HVcache_size)),
         HVcache_nodeP(new treeNode *[size0f_HVcache +1]()),
         HVcache_aliveState(new bool[size0f_HVcache +1]())
        {}

        Tree256(const Tree256 &) = delete;
        Tree256 &operator=(const Tree256 &) = delete;

            
        /*
            input-- path of node, length of nodePath
            output-- pointer
            do what-- return the pointer points to the node if it exists, or nullptr
        */
        treeNode** _find_node(uint8_t* nodePathP, size_t length)
        {
            if (!_node_isExist(nodePathP, length)) {return nullptr; }
            treeNode **nodeTP = &rootNodeP;
            if (tempNodeP != nullptr || *tempNodeP != nullptr)
            {
                nodeTP = tempNodeP;
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
            input-- path of node(must be exist), length of nodePath
            output-- true if success, false if not
            do what-- create a children-node-pointer array with nullptr-filled and mount it to the node
            tips-- if the node already points to a children-node-pointer array, it will do nothing and return true
        */
        bool createChildren(uint8_t* nodePathP, size_t length)
        {
            treeNode **nodeP = _find_node(nodePathP, length);
            if (nodeP == nullptr || *nodeP == nullptr) {return false; }
            if ((*nodeP)->childFP != nullptr) {return true; }
            (*nodeP)->childFP = new treeNode *[256]();
            return true;
        }

        /* pause */
        bool free_node(uint8_t* nodePathP, size_t length)
        {return false; }

        ~Tree256()
        {
            _free_nodeTree(&rootNodeP);
            //delete rootNodeP;
            delete[] HVcache_nodeP;
            delete[] HVcache_aliveState;
        }

    };


};




