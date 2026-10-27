// testMain.cpp -- CKtree256 API smoke test
// 编译： g++ -std=c++20 -Wall -Wextra -Wpedantic testMain.cpp -o cktree_test
// 说明： classBasic.cpp 目前是“头文件式”实现，直接把整份实现包含进来。
//        当前覆盖 root/空 path、单字节/多字节 key、前缀数据、嵌入 0、覆盖写、缺失 path、非法参数、erase_data、free_node 占位。
//        external link / HVcache 目前只有内部实现，尚无公开 node 入口；free_node 仍是占位，不在本测试范围。

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "classBasic.cpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    CKtree::Tree256 tree(1024);   // 缓存槽数 1024（内部下限 0xff）

    int rootData = 1;
    int oneData = 2;
    int oneData2 = 3;
    int twoData = 4;
    int zeroData = 5;
    int deepData = 6;
    int missingData = 7;

    uint8_t keyOne[1] = {0x41};
    uint8_t keyOneZero[2] = {0x41, 0x00};
    uint8_t keyZero[1] = {0x00};
    uint8_t keyDeep[3] = {0x41, 0x42, 0x43};
    uint8_t keyMissing[2] = {0x12, 0xAB};
    uint8_t keyMissingLong[3] = {0x12, 0xAB, 0x05};
    uint8_t emptyPath[1] = {0xFF};   // 非空指针 + length 0，语义上仍指向根

    // 0) 空树：根节点存在，但还没有数据
    assert(tree.get_data(nullptr, 0) == nullptr);
    assert(tree.get_data(keyOne, 1) == nullptr);

    // 1) 根节点写入：nullptr + 0 和 非空指针 + 0 都应落到根
    assert(tree.put_data(nullptr, 0, &rootData));
    assert(tree.get_data(nullptr, 0) == &rootData);
    assert(tree.get_data(emptyPath, 0) == &rootData);

    // 2) 单字节 key；get_data 的 length 决定节点，不依赖 C 字符串
    assert(tree.get_data(keyOne, 1) == nullptr);
    assert(tree.put_data(keyOne, 1, &oneData));
    assert(tree.get_data(keyOne, 1) == &oneData);
    assert(tree.get_data(keyOne, 0) == &rootData);

    // 3) 前缀和子路径是不同节点；插入子节点不能覆盖父节点数据
    assert(tree.get_data(keyOneZero, 2) == nullptr);
    assert(tree.put_data(keyOneZero, 2, &twoData));
    assert(tree.get_data(keyOneZero, 2) == &twoData);
    assert(tree.get_data(keyOneZero, 1) == &oneData);
    assert(tree.get_data(keyOne, 1) == &oneData);

    // 4) 中间节点可以没有 data；缺失 path 和“存在但无 data”目前都返回 nullptr
    assert(tree.put_data(keyDeep, 3, &deepData));
    assert(tree.get_data(keyDeep, 3) == &deepData);
    assert(tree.get_data(keyDeep, 2) == nullptr);
    assert(tree.get_data(keyMissing, 2) == nullptr);
    assert(tree.get_data(keyMissingLong, 3) == nullptr);

    // 5) 嵌入 0 和首字节 0 都是合法 key 字节
    assert(tree.put_data(keyZero, 1, &zeroData));
    assert(tree.get_data(keyZero, 1) == &zeroData);
    assert(tree.get_data(emptyPath, 0) == &rootData);

    // 6) 同一 path 重复 put_data：覆盖指针，不释放旧 data
    assert(tree.put_data(keyOne, 1, &oneData2));
    assert(tree.get_data(keyOne, 1) == &oneData2);
    assert(tree.get_data(keyOneZero, 2) == &twoData);

    // 7) 缺失 path 可以通过 put_data 建立；更长前缀仍然缺失
    assert(tree.put_data(keyMissing, 2, &missingData));
    assert(tree.get_data(keyMissing, 2) == &missingData);
    assert(tree.get_data(keyMissingLong, 3) == nullptr);

    // 8) 非法参数：nullptr + 非零长度、空 data
    assert(!tree.put_data(nullptr, 1, &rootData));
    assert(!tree.put_data(keyOne, 1, nullptr));
    assert(tree.get_data(nullptr, 1) == nullptr);

    // 9) 已有接口回归：createChildren 幂等，free_node 仍是占位返回 false
    assert(tree.createChildren(nullptr, 0));
    assert(tree.createChildren(keyOne, 1));
    assert(tree.createChildren(keyOne, 1));
    assert(!tree.createChildren(keyMissingLong, 3));
    assert(tree.get_data(keyOne, 1) == &oneData2);
    assert(!tree.free_node(keyOne, 1, false));
    assert(tree.get_data(nullptr, 0) == &rootData);

    // 10) erase_data：缺失 path false；已存在节点清数据（已经为空也返回 true）
    assert(!tree.erase_data(keyMissingLong, 3));
    assert(tree.erase_data(keyZero, 1));
    assert(tree.get_data(keyZero, 1) == nullptr);
    assert(tree.erase_data(keyZero, 1));

    std::puts("[test] put_data / get_data / erase_data / createChildren / free_node OK");
    std::puts("[test] TODO: external link / HVcache 缺公开 node 入口，free_node 仍未实现");
    return 0;
}
