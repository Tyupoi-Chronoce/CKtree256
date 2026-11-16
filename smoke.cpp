// smoke.cpp -- CKtree256 public API smoke test
//
// DSH-managed smoke test. Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -Wpedantic smoke.cpp -o smoke
//   ./smoke
//
// testMain.cpp is intentionally left as the user's own experimental entry point.
// This file includes classBasic.cpp directly because the project has no build system yet.
//
// Coverage: root/empty path, single-byte and multi-byte keys, prefix nodes,
// embedded 0, overwrite, missing paths, invalid arguments, createChildren,
// link_node argument checks and successful path copy, path-based get_linkNode,
// repeated jumps that cross the target-side hitCounts_4beJumped threshold
// (cache admission and cache hits), relink, missing source/target, free_nodeTree
// while the freed targets are cached, and the independent 0x00 branch.
// Not covered: the private _cache_eviction sweep/fallback policy (it has no caller
// yet), pinning, concurrent access, and allocation-failure paths.

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "classBasic.cpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    CKtree::Tree256 tree(1024);   // HVcache slots; the constructor clamps to 0xff minimum

    int rootData = 1;
    int aData = 2;
    int abData = 3;
    int abcData = 4;
    int zeroData = 5;
    int overwrittenData = 6;

    std::uint8_t keyA[1] = {'A'};
    std::uint8_t keyAB[2] = {'A', 'B'};
    std::uint8_t keyABC[3] = {'A', 'B', 'C'};
    std::uint8_t keyZero[1] = {0x00};
    std::uint8_t keyMissing[2] = {0x12, 0xAB};
    std::uint8_t keyMissingLong[3] = {0x12, 0xAB, 0x05};
    std::uint8_t emptyPath[1] = {0xFF};   // non-null pointer + length 0 still means root

    // 0) Empty tree: the root node exists, but no node holds data yet.
    assert(tree.get_data(nullptr, 0) == nullptr);
    assert(tree.get_data(keyA, 1) == nullptr);

    // 1) Root: nullptr + 0 and any pointer + 0 both address the root.
    assert(tree.put_data(nullptr, 0, &rootData));
    assert(tree.get_data(nullptr, 0) == &rootData);
    assert(tree.get_data(emptyPath, 0) == &rootData);

    // 2) One-byte key: length is explicit, so embedded 0 and prefixes are safe.
    assert(tree.get_data(keyA, 1) == nullptr);
    assert(tree.put_data(keyA, 1, &aData));
    assert(tree.get_data(keyA, 1) == &aData);
    assert(tree.get_data(keyA, 0) == &rootData);

    // 3) Prefix nodes stay distinct from their descendants.
    assert(tree.get_data(keyAB, 2) == nullptr);
    assert(tree.put_data(keyAB, 2, &abData));
    assert(tree.get_data(keyAB, 2) == &abData);
    assert(tree.get_data(keyAB, 1) == &aData);
    assert(tree.get_data(keyA, 1) == &aData);

    // 4) Intermediate nodes may have no data; missing paths also return nullptr.
    assert(tree.put_data(keyABC, 3, &abcData));
    assert(tree.get_data(keyABC, 3) == &abcData);
    assert(tree.get_data(keyABC, 2) == &abData);
    assert(tree.get_data(keyMissing, 2) == nullptr);
    assert(tree.get_data(keyMissingLong, 3) == nullptr);

    // 5) A leading 0x00 is a normal key byte, not a terminator.
    assert(tree.put_data(keyZero, 1, &zeroData));
    assert(tree.get_data(keyZero, 1) == &zeroData);
    assert(tree.get_data(emptyPath, 0) == &rootData);

    // 6) Repeating put_data only overwrites the data pointer; descendants are intact.
    assert(tree.put_data(keyA, 1, &overwrittenData));
    assert(tree.get_data(keyA, 1) == &overwrittenData);
    assert(tree.get_data(keyAB, 2) == &abData);
    assert(tree.get_data(keyABC, 3) == &abcData);

    // 7) Invalid arguments: nullptr + non-zero length, and null data.
    assert(!tree.put_data(nullptr, 1, &rootData));
    assert(!tree.put_data(keyA, 1, nullptr));
    assert(tree.get_data(nullptr, 1) == nullptr);
    assert(!tree.erase_data(nullptr, 1));

    // 8) createChildren is idempotent and refuses a missing path.
    assert(tree.createChildren(nullptr, 0));
    assert(tree.createChildren(keyA, 1));
    assert(tree.createChildren(keyA, 1));
    assert(!tree.createChildren(keyMissing, 2));
    assert(tree.get_data(keyA, 1) == &overwrittenData);

    // 9) link_node: both endpoints must exist, and the target path is copied
    //    (allocation failure propagates as std::bad_alloc). get_linkNode takes the
    //    source path, so the link/cache path is now usable without a public handle.
    assert(tree.get_linkNode(keyA, 1) == nullptr);        // no link yet
    assert(!tree.link_node(keyMissing, 2, keyA, 1));      // missing destination
    assert(!tree.link_node(keyA, 1, keyMissing, 2));      // missing target
    assert(!tree.link_node(keyA, 1, nullptr, 1));         // invalid target path
    assert(tree.link_node(keyA, 1, nullptr, 0));          // A -> root, valid
    CKtree::treeNode* rootNode = tree.get_linkNode(keyA, 1);
    assert(rootNode != nullptr);                          // length 0 resolves to the root

    assert(tree.link_node(keyA, 1, keyAB, 2));            // relink A -> AB
    CKtree::treeNode* linkedAB = tree.get_linkNode(keyA, 1);
    assert(linkedAB != nullptr);
    assert(linkedAB != rootNode);
    for (int i = 0; i < 20; ++i)                          // cross the threshold, then hit the cache
    {
        assert(tree.get_linkNode(keyA, 1) == linkedAB);
    }

    assert(tree.link_node(keyA, 1, keyABC, 3));           // relink A -> ABC
    CKtree::treeNode* linkedABC = tree.get_linkNode(keyA, 1);
    assert(linkedABC != nullptr);
    assert(linkedABC != linkedAB);
    for (int i = 0; i < 20; ++i)
    {
        assert(tree.get_linkNode(keyA, 1) == linkedABC);
    }

    assert(tree.link_node(keyA, 1, nullptr, 0));          // relink A -> root again
    assert(tree.get_linkNode(keyA, 1) == rootNode);
    assert(tree.get_linkNode(keyMissing, 2) == nullptr);  // missing source path

    // 10) free_nodeTree releases A, AB and ABC (the targets were cached above),
    //     but leaves the root alone.
    assert(!tree.free_nodeTree(nullptr, 0));              // root cannot be released
    assert(tree.free_nodeTree(keyA, 1));
    assert(tree.get_data(keyA, 1) == nullptr);
    assert(tree.get_data(keyAB, 2) == nullptr);
    assert(tree.get_data(keyABC, 3) == nullptr);
    assert(tree.get_linkNode(keyA, 1) == nullptr);        // source is gone
    assert(tree.get_data(nullptr, 0) == &rootData);
    assert(!tree.free_nodeTree(keyA, 1));                 // already gone

    // 11) The independent 0x00 branch survives the A-subtree release.
    assert(tree.get_data(keyZero, 1) == &zeroData);
    assert(tree.erase_data(keyZero, 1));
    assert(tree.get_data(keyZero, 1) == nullptr);
    assert(tree.erase_data(keyZero, 1));                  // erasing an existing empty node is true
    assert(tree.free_nodeTree(keyZero, 1));
    assert(tree.get_data(nullptr, 0) == &rootData);

    std::puts("[smoke] put_data / get_data / erase_data / createChildren / link_node / get_linkNode / free_nodeTree OK");
    std::puts("[smoke] target-side hitCounts_4beJumped admission, cache hit, relink and cached-target free OK");
    return 0;
}
