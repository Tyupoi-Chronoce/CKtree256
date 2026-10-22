// testMain.cpp -- CKtree256 最小 smoke demo
// 编译： g++ -std=c++20 testMain.cpp -o cktree_demo
// 说明： classBasic.cpp 目前是“头文件式”实现，直接把整份实现包含进来。
//        当前还没有 insert / find-value，本 demo 只验证已有接口的行为。

#include "classBasic.cpp"

#include <cassert>
#include <cstdio>

int main()
{
    CKtree::Tree256 tree(1024);   // 缓存槽数 1024（内部下限 0xff）

    // 1) 根节点的子指针数组：可以创建，且是幂等的
    assert(tree.createChildren(nullptr, 0) == true);
    assert(tree.createChildren(nullptr, 0) == true);

    // 2) 路径上的节点还不存在：createChildren 应该安全地返回 false，而不是崩溃
    uint8_t missingPath[] = {0x12, 0xAB, 0x05};
    assert(tree.createChildren(missingPath, 3) == false);

    // 3) _find_node 已改为内部接口，demo 不再直接访问；
    //    缺失路径的行为由上面 createChildren(missingPath, 3) == false 覆盖
    // 4) public free_node 仍是占位实现（TODO），当前约定返回 false
    assert(tree.free_node(missingPath, 3) == false);

    std::puts("[demo] createChildren / free_node 行为符合预期");
    std::puts("[demo] TODO: insert、find-value、external path 跳转、HVcache 准入淘汰尚未实现");
    return 0;
}
