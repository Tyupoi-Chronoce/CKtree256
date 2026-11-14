// smoke.cpp -- CKtree256 public API smoke test
//
// DSH-managed smoke test. Build and run from the repository root:
//   g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic smoke.cpp -o smoke
//   ./smoke
//
// testMain.cpp is intentionally left as the user's own experimental entry point.
// This file includes classBasic.cpp directly because the project has no build system yet.
//
// Coverage: root/empty path, single-byte and multi-byte keys, prefix nodes,
// embedded 0, overwrite, missing paths, invalid arguments, createChildren,
// link_node argument checks, and free_nodeTree.
// Not covered: successful link_node -> get_linkNode jumps, because get_linkNode
// still takes treeNode<T>** and there is no public node-handle accessor yet.

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "classBasic.cpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    CKtree::Tree256<int> tree(1024);   // typed tree; HVcache slots clamp to 0xff minimum

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

    // 9) link_node: both endpoints must exist. Successful links cannot be read by
    //    this smoke test because get_linkNode still needs a treeNode** handle.
    assert(!tree.link_node(keyMissing, 2, keyA, 1));      // missing destination
    assert(!tree.link_node(keyA, 1, keyMissing, 2));      // missing target
    assert(!tree.link_node(keyA, 1, nullptr, 1));         // invalid target path
    assert(tree.link_node(keyA, 1, nullptr, 0));          // A -> root, valid
    assert(tree.link_node(keyA, 1, keyAB, 2));            // relink A -> AB

    // 10) free_nodeTree releases A, AB and ABC, but leaves the root alone.
    assert(!tree.free_nodeTree(nullptr, 0));              // root cannot be released
    assert(tree.free_nodeTree(keyA, 1));
    assert(tree.get_data(keyA, 1) == nullptr);
    assert(tree.get_data(keyAB, 2) == nullptr);
    assert(tree.get_data(keyABC, 3) == nullptr);
    assert(tree.get_data(nullptr, 0) == &rootData);
    assert(!tree.free_nodeTree(keyA, 1));                 // already gone

    // 11) The independent 0x00 branch survives the A-subtree release.
    assert(tree.get_data(keyZero, 1) == &zeroData);
    assert(tree.erase_data(keyZero, 1));
    assert(tree.get_data(keyZero, 1) == nullptr);
    assert(tree.erase_data(keyZero, 1));                  // erasing an existing empty node is true
    assert(tree.free_nodeTree(keyZero, 1));
    assert(tree.get_data(nullptr, 0) == &rootData);

    std::puts("[smoke] put_data / get_data / erase_data / createChildren / link_node / free_nodeTree OK");
    std::puts("[smoke] TODO: successful link_node -> get_linkNode needs a public node handle");
    return 0;
}
