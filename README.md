# CKtree256

An experimental **256-way byte-radix tree**. The long-term goal is a persistent, cross-process, dangling-pointer-free way to reference nodes inside and across branches: use **logical byte paths** instead of raw pointers, and a **hot-path cache** to flatten cross-branch jumps to O(1).

**Languages:** English · [简体中文](README.zh-CN.md)

---

## Overview

The long-term design is described in `introduction.md` (kept local, not published; see `.gitignore`). This repository currently contains:

- `classBasic.cpp`: the single-file development implementation; the smoke test still includes it directly;
- `_4lib_work/CKtree256.hpp` + `.cpp`: the **library-format** split (declarations / definitions separated);
- `lib/libCKtree256.a` + `lib/CKtree256.hpp`: the built static library and its public header;
- `testMain.cpp`: a smoke test for the current public API.

> **Status: pre-alpha, structural prototype.**
> The tree can create child-pointer arrays, read/write/clear `dataP` along byte paths, and recursively free a whole tree. HVcache has a threshold + free-slot admission shell, but no eviction or pinning.
> **Not implemented yet:** a publicly usable cross-branch node handle, cache eviction/pinning, persistence, a formal test framework, or a build system. `free_node` is still a placeholder.
> The API will change significantly. **Do not use this in production.**

## Repository layout

| File / directory | Description |
|---|---|
| `classBasic.cpp` | Single-file development implementation (header-style .cpp, no `main`, no include guard); `testMain.cpp` includes it directly |
| `_4lib_work/CKtree256.hpp` | Public header of the library split: class declaration, public API comments (English + Chinese) |
| `_4lib_work/CKtree256.cpp` | Library implementation: `Tree256` member definitions, internal `treeNode` / cache helper types |
| `lib/libCKtree256.a` | Static library (C++ ABI; built with GCC 14.2.0 / MinGW-w64) |
| `lib/CKtree256.hpp` | Public header copy shipped with the library |
| `testMain.cpp` | Public API smoke test (includes `classBasic.cpp`, does not link the library) |
| `introduction.md` | Design document, kept locally and not committed |
| `README.zh-CN.md` | Chinese version of this README |
| `LICENSE` | MIT license |

> Maintenance relationship: `classBasic.cpp` is the development source; `_4lib_work` is its library-format snapshot. The two can drift temporarily and should be synchronized before a release. Both the single-file version and the library copy currently build warning-free with GCC 14.2.0 / MinGW-w64.

## What works today

- `createChildren(path, len)` — allocate a 256-slot child-pointer array for an existing node (idempotent; safely returns false when the path is missing)
- `put_data(path, len, dataP)` — ensure the node exists along the path and write `dataP`; writing the same path again only overwrites the pointer and does not free the old data; `dataP` must be non-null and the tree does not own it
- `get_data(path, len)` — read `dataP` by path; returns `nullptr` when the path is missing or the node has no data; `length == 0` reads the root
- `erase_data(path, len)` — clear `dataP` on an existing node without deleting the node; returns false when the path is missing
- `link_node(dstNodeP, path, len)` — copy the target's logical path onto the `dstNodeP` node; a repeated call replaces the old path; the caller must already have a `treeNode**` handle
- `get_linkNode(nodeP)` — resolve an external link: on a miss it walks the path; once the source hit count passes the threshold (`minCounts_move2cache = 0x0f`), it tries to admit the target to HVcache; later hits use the slot table. On a cache hit the returned handle points at the cache slot, not at the target's original parent slot; do not rely on pointer identity
- `_mount_node(path, len)` / `_find_node(path, len)` / `_create_childrenArray(slot)` / `_free_nodeTree(&slot)` — internal helpers; the caller must hold the lock (see "Threading and locking")
- `HVcache_nodeP` / `HVcache_aliveState` — a non-owning pointer table with `size + 1` slots; slot 0 is a sentinel; `_can_putInto_cache` linearly scans for an already-cached target first, then takes the first free slot
- `privateMutex` — protects the tree and cache; public entry points take it once per call, private helpers require the caller to hold it
- `free_node(path, len, freeData)` — public placeholder that always returns false; not implemented yet

## Static library

The repository already contains the built artifacts:

```text
lib/CKtree256.hpp      # public header
lib/libCKtree256.a     # static library
```

Rebuild from `_4lib_work`:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

Link a consumer:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilib app.cpp lib/libCKtree256.a -o app
```

Only a **C++ class interface** is provided; there is no C-style / `extern "C"` header yet. `Tree256` depends on `std::mutex`, and `link_node` / `get_linkNode` expose a `treeNode**` handle. If C, Python, or another language needs access later, add a separate opaque-handle + `extern "C"` wrapper instead of putting the internal `treeNode` in a C header. Keeping the C++ static library for now avoids freezing an ABI too early.

## Build and run

Single-file smoke test (still includes `classBasic.cpp` directly):

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic testMain.cpp -o ckt_test
./ckt_test
```

Expected output:

```text
[test] put_data / get_data / erase_data / createChildren / free_node OK
[test] TODO: external link / HVcache 缺公开 node 入口，free_node 仍未实现
```

The current version builds with zero warnings under GCC 14.2.0 / MinGW-w64, and the test exits with code 0. The library split was also built with the same `-Wall -Wextra -Wpedantic` flags and verified through a temporary consumer probe linked against `lib/libCKtree256.a` (put/get/erase/createChildren/destructor, exit 0).

## TODO / known issues

Ordered roughly by priority; contributions are welcome:

1. **API not finalized**: the public API has `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, and a placeholder `free_node`. There is no public node-handle accessor yet, so external callers cannot actually use `link_node` / `get_linkNode`. `free_node` semantics (single node vs. whole subtree, whether data is released, how external links and cache entries are invalidated) are undecided.
2. **Key and data contract**: explicit lengths support embedded 0, but the external-path format still needs a length or terminator convention, and `dataP` ownership (currently: the tree never deletes it; the caller owns it) needs to be finalized. `get_data` cannot currently distinguish "path missing" from "node exists but has no data".
3. **Cache is only a shell**: the threshold is now wired into `get_linkNode`, but there is no eviction (LRU / CLOCK) and no pinning (a cached node must not be deleted); `_can_putInto_cache` linearly scans the slot table and takes the first free slot, without distinguishing "newly inserted" from "already inserted by another source"; the two `_put2cache` overloads duplicate logic, and their return semantics need to be split. Node destruction currently clears a slot only by checking `self_in_cache` plus the slot pointer; if the flag and the slot disagree, a dangling slot can remain.
4. **Cross-branch references are only half done**: the link now stores a copied logical path, but there is no path validation. After the structure changes, an old path can silently point to a different node; path hashing or a global revision is needed. A public node handle is also missing.
5. **Child representation wastes memory**: a fixed 256 pointers per node = 2 KB, which is very wasteful for sparse trees. A planned fix is a bitmap + compact array, or ART-style Node4/16/48/256.
6. **`tempNodeP_4easyFind` implicit state**: `_node_isExist` writes the result through a member variable and `_find_node` reads and clears it; a failure or early return can leave a stale value, and `_find_node` still contains a duplicate traversal that is effectively unreachable. Return the slot pointer directly instead of going through a member variable.
7. **Thread safety is unverified**: `privateMutex` is wired into `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, `_put2cache` (now caller-locked; no internal lock), and the destructor; `_find_node` / `_node_isExist` / `_mount_node` / `_can_putInto_cache` / `_free_nodeTree` depend on the caller holding the lock; `free_node` has no locking contract, and lock granularity is undecided.
8. **Persistence cannot "just dump" yet**: `childFP` is a raw pointer; before it goes cross-process or to disk it must become an arena + offsets, or be serialized as node IDs.
9. **No formal tests or CI**: `testMain.cpp` covers the current public API with a smoke test, and the library split has a temporary consumer probe; but there is no unit-test framework, CMake, or GitHub Actions.
10. **Recursive free depth = key length**: the author is aware of and accepts that very deep keys may overflow the stack; this can later be converted to an explicit stack.
11. **Naming legacy**: `size0f_HVcache`, `length_externaRelatedlLink_nodePath`, `self_cacheIndex`, and similar spelling/style issues need to be unified.
12. **Exception safety**: `_mount_node` / `_create_childrenArray` can leave partial nodes or an empty path behind if `new` throws; there is no rollback yet.

## Design notes

- 256-way fanout = consume one byte per level; tree depth equals the key's byte length
- A cross-branch reference stores the target's **byte path**, not a physical pointer, avoiding dangling pointers
- Once a hot-path hit count passes a threshold, the target's physical pointer is placed in an in-tree cache slot to make the jump O(1)
- The cache is **non-owning**: a node placed in the cache must not be deleted directly; the eviction / pinning protocol is not implemented yet
- The library keeps a C++ class interface; a C ABI would need a separate opaque-handle wrapper layer and is intentionally not provided yet

## Threading and locking

- `privateMutex` protects the root pointer, tree structure, and HVcache slot table.
- Public entry points that lock internally: `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, `~Tree256`.
- Private helpers that require the caller to hold the lock: `_find_node`, `_node_isExist`, `_mount_node`, `_create_childrenArray`, `_can_putInto_cache`, `_remove_from_cache`, `_put2cache`, `_find_from_cache`, `_free_nodeTree` (the destructor holds the lock while recursing into `_free_nodeTree`; the recursion itself does not lock again).
- `free_node` is not implemented and has no locking contract yet; public entry points take the lock once per call and do not call an already-locking public function.

## Contributing

At this stage, **design discussion** matters more than code volume. Issues about node layout, cache policy, and persistence are welcome. Before opening a PR, please explain the problem it solves.

## License

[MIT](LICENSE), Copyright (c) 2026 Tyupoi-Chronoce. See [LICENSE](LICENSE) for the full text.

## Acknowledgements

The project started as a discussion about "path as coordinate" — from `0xCC` mojibake, to cross-branch path redirection, to imagining the hot-path cache as a synapse. Thanks to everyone who is willing to help get it right.

---

**Languages:** English · [简体中文](README.zh-CN.md)
