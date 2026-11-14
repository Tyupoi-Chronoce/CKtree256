# CKtree256

An experimental **256-way byte-radix tree**. The long-term goal is a persistent, cross-process, dangling-pointer-free way to reference nodes inside and across branches: use **logical byte paths** instead of raw pointers, and a **hot-path cache** to flatten cross-branch jumps to O(1).

**Languages:** English · [简体中文](README.zh-CN.md)

---

## Overview

The long-term design is described in `introduction.md` (kept local, not published; see `.gitignore`). This repository currently contains:

- `classBasic.cpp`: the single-file development implementation; it defines the class template `Tree256<T>`, and the smoke test includes it directly;
- `smoke.cpp`: the DSH-managed public-API smoke test; it instantiates `Tree256<int>`;
- `testMain.cpp`: the owner's scratch entry point, intentionally left as a minimal compilable stub;
- `_4lib_work/CKtree256.hpp` + `.cpp`: a **library-format snapshot** from 2026-10-28;
- `lib/libCKtree256.a` + `lib/CKtree256.hpp`: prebuilt artifacts from that snapshot;
- `docs/C_API_design.md`: a design draft for a future C ABI (not implemented);
- `docs/VSCode_Git_workflow.md`: VSCode Source Control / Git workflow notes (Chinese).

> **Status: pre-alpha, structural prototype.**
> The tree can create child-pointer arrays, read/write/clear a `T*` data pointer along byte paths, link one existing node to another by logical path, resolve those links when handed a node handle, and recursively free a subtree. HVcache has threshold/free-slot admission plus per-slot generation checks, but no eviction or pinning.
> **Not implemented yet:** a publicly usable cross-branch node handle, cache eviction/pinning, path validation after structural changes, persistence, a formal test framework, or a build system. `get_linkNode` still takes a `treeNode<T>**`, so external callers cannot yet use the link/cache path end-to-end.
> The API will change significantly. **Do not use this in production.**

## Repository layout

| File / directory | Description |
|---|---|
| `classBasic.cpp` | Single-file development implementation (header-style .cpp, no `main`, no include guard); defines the class template `Tree256<T>`; `smoke.cpp` includes it directly |
| `smoke.cpp` | DSH-managed smoke test for the current public API; instantiates `Tree256<int>` (includes `classBasic.cpp`, does not link the library) |
| `testMain.cpp` | Owner-maintained experimental entry point; DSH no longer edits it |
| `_4lib_work/CKtree256.hpp` | Public header of the library-format snapshot (older API) |
| `_4lib_work/CKtree256.cpp` | Library-format implementation snapshot (older API) |
| `lib/libCKtree256.a` | Static library built from the 2026-10-28 snapshot (C++ ABI; GCC 14.2.0 / MinGW-w64) |
| `lib/CKtree256.hpp` | Public header copy shipped with the snapshot library |
| `docs/C_API_design.md` | C ABI design draft; not implemented |
| `docs/VSCode_Git_workflow.md` | VSCode Source Control / Git workflow notes (Chinese) |
| `introduction.md` | Original design discussion, kept locally and not committed |
| `README.zh-CN.md` | Chinese version of this README |
| `LICENSE` | MIT license |

> Maintenance relationship: `classBasic.cpp` is the development source. `_4lib_work` and `lib` are an **older snapshot** (2026-10-28) and do not yet contain the path-based `link_node` or `free_nodeTree`; rebuild or resync them before treating them as current. The single-file source currently builds warning-free with GCC 14.2.0 / MinGW-w64, and `smoke.cpp` instantiates `Tree256<int>`.

## What works today

`Tree256<T>` is a class template. `T` is the caller's data type; each node stores a `T*` (`dataP`), and the tree never owns that data. `smoke.cpp` instantiates `Tree256<int>`. Because `classBasic.cpp` is still included directly, the template definition is visible to that translation unit.

- `createChildren(path, len)` — allocate a 256-slot child-pointer array for an existing node (idempotent; safely returns false when the path is missing)
- `put_data(path, len, T* dataP)` — ensure the node exists along the path and write `dataP`; writing the same path again only overwrites the pointer and does not free the old data; `dataP` must be non-null and the tree does not own it
- `get_data(path, len)` — read the stored `T*` by path; returns `nullptr` when the path is missing or the node has no data; `length == 0` reads the root
- `erase_data(path, len)` — clear `dataP` on an existing node without deleting the node; returns false when the path is missing
- `link_node(dstPath, dstLen, targetPath, targetLen)` — link an existing destination node to an existing target node by logical path; repeated calls replace the old path; both endpoints must already exist
- `get_linkNode(nodeP)` — resolve a link from a `treeNode<T>**` handle; on a miss it walks the stored path, and once the source hit count passes the threshold (`minCounts_move2cache = 0x0f`) it tries to admit the target to HVcache. On a cache hit it validates the slot index, generation and target flags, then returns a pointer into the cache slot — not the target's original parent slot. A stale slot is discarded and the path is used again.
- `free_nodeTree(path, len)` — recursively release a subtree, each node's copied external path and its own cache slot; it does **not** free `dataP` and refuses to release the root; missing paths return false
- Internal helpers such as `_mount_node`, `_find_node`, `_create_childrenArray`, `_free_nodeTree`, `_can_putInto_cache`, `_put2cache`, `_remove_self_from_cache` and `_forget_link_cache` — the caller must hold the lock
- `HVcache_nodeP` / `HVcache_aliveState` / `HVcache_generation` — a non-owning pointer table with `size + 1` slots; slot 0 is a sentinel. `_can_putInto_cache` linearly scans for an already-cached target first, then takes the first free slot, and reports whether the entry is new.
- `privateMutex` — protects the tree and cache; public entry points take it once per call, private helpers require the caller to hold it

## Library-format snapshot

The repository contains an older split of the project:

```text
_4lib_work/CKtree256.hpp      # snapshot public header
_4lib_work/CKtree256.cpp      # snapshot implementation
lib/CKtree256.hpp             # header copy
lib/libCKtree256.a            # static library built from the snapshot
```

Rebuild from the snapshot:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

Link a consumer:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilib app.cpp lib/libCKtree256.a -o app
```

The snapshot still exposes the old `free_node` / `link_node(treeNode**, ...)` API; it is **not** a build of the current `classBasic.cpp`. Only a C++ class interface is provided; there is no C-style / `extern "C"` header yet. The current `Tree256<T>` depends on `std::mutex`, and `get_linkNode` exposes a `treeNode<T>**` handle. A future C ABI would need a separate opaque-handle wrapper layer; see `docs/C_API_design.md`.

## Build and run

Single-file smoke test (includes `classBasic.cpp` directly):

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic smoke.cpp -o smoke
./smoke
```

Expected output:

```text
[smoke] put_data / get_data / erase_data / createChildren / link_node / free_nodeTree OK
[smoke] TODO: successful link_node -> get_linkNode needs a public node handle
```

The current version builds with zero warnings under GCC 14.2.0 / MinGW-w64, and the smoke test (`Tree256<int>`) exits with code 0. `testMain.cpp` is the owner's scratch file; its current stub also compiles, but it is not part of the smoke coverage.

## TODO / known issues

Ordered roughly by priority; contributions are welcome:

1. **API not finalized**: `Tree256<T>` requires an explicit data type (there is no default template argument), and the public API has `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode` and `free_nodeTree`. There is no public node-handle accessor yet, so external callers cannot use `get_linkNode` / the cache path end-to-end. `free_nodeTree` frees structure and cache slots, but the data deleter and single-node-vs-subtree semantics still need to be finalized.
2. **Key and data contract**: explicit lengths support embedded 0, but the external-path format still needs a length or terminator convention, and `dataP` ownership (currently: the tree never deletes it; the caller owns it) needs to be documented as a hard contract. `get_data` cannot currently distinguish "path missing" from "node exists but has no data".
3. **Cache is not finished**: the threshold and generation checks are wired into `get_linkNode`, but there is no eviction (LRU / CLOCK) and no pinning (a cached node must not be deleted). `_can_putInto_cache` linearly scans the slot table and takes the first free slot; the two `_put2cache` overloads duplicate logic and their return semantics need to be split. A cache hit returns a pointer into the slot table, not a stable node handle.
4. **Cross-branch references are only half done**: the link stores a copied logical path, but there is no path validation. After the structure changes, an old path can silently point to a different node; path hashing or a global revision is needed. A public node identity/handle is also missing.
5. **Child representation wastes memory**: a fixed 256 pointers per node = 2 KB, which is very wasteful for sparse trees. A planned fix is a bitmap + compact array, or ART-style Node4/16/48/256.
6. **`tempNodeP_4easyFind` implicit state**: `_node_isExist` writes the result through a member variable and `_find_node` reads and clears it; a failure or early return can leave a stale value, and the duplicate traversal in `_find_node` is effectively unreachable. Return the slot pointer directly instead of going through a member variable.
7. **Thread safety is unverified**: `privateMutex` is wired into `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, `free_nodeTree` and the destructor; private helpers require the caller to hold the lock. There is no concurrency test, and lock granularity is undecided.
8. **Persistence cannot "just dump" yet**: `childFP` is a raw pointer; before it goes cross-process or to disk it must become an arena + offsets, or be serialized as node IDs.
9. **No formal tests or CI**: `smoke.cpp` covers the current public API with `Tree256<int>`, and `testMain.cpp` is the owner's scratch file; there is no unit-test framework, CMake, or GitHub Actions.
10. **Recursive free depth = key length**: the author is aware of and accepts that very deep keys may overflow the stack; this can later be converted to an explicit stack.
11. **Naming legacy**: `size0f_HVcache`, `length_externaRelatedlLink_nodePath`, `self_cacheIndex`, and similar spelling/style issues need to be unified.
12. **Exception safety**: `_mount_node` / `_create_childrenArray` use throwing `new`, while `_link_node` uses nothrow `new`; the `bool` public API therefore has an inconsistent exception contract.
13. **Snapshots can drift**: `_4lib_work` and `lib` are older than `classBasic.cpp`. Sync them (or remove the artifacts) before a release so consumers do not compile against the old API.

## Design notes

- 256-way fanout = consume one byte per level; tree depth equals the key's byte length
- A cross-branch reference stores the target's **byte path**, not a physical pointer, avoiding dangling pointers
- Once a hot-path hit count passes a threshold, the target's physical pointer is placed in an in-tree cache slot to make the jump O(1)
- The cache is **non-owning**: a node placed in the cache must not be deleted directly; the eviction / pinning protocol is not implemented yet
- Slot entries have a generation; a cache hit must match both the source's recorded generation and the target's self-cache flags, otherwise the source falls back to the path
- The library keeps a C++ class interface; a C ABI would need a separate opaque-handle wrapper layer and is intentionally not provided yet

## Threading and locking

- `privateMutex` protects the root pointer, tree structure, and HVcache slot table.
- Public entry points that lock internally: `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, `free_nodeTree`, `~Tree256`.
- Private helpers that require the caller to hold the lock: `_find_node`, `_node_isExist`, `_mount_node`, `_create_childrenArray`, `_can_putInto_cache`, `_remove_self_from_cache`, `_forget_link_cache`, `_put2cache`, `_find_from_cache`, `_free_nodeTree` (the destructor holds the lock while recursing into `_free_nodeTree`; the recursion itself does not lock again).
- `free_nodeTree` refuses to release the root; public entry points take the lock once per call and do not call an already-locking public function.

## Contributing

At this stage, **design discussion** matters more than code volume. Issues about node layout, cache policy, and persistence are welcome. Before opening a PR, please explain the problem it solves.

## License

[MIT](LICENSE), Copyright (c) 2026 Tyupoi-Chronoce. See [LICENSE](LICENSE) for the full text.

## Acknowledgements

The project started as a discussion about "path as coordinate" — from `0xCC` mojibake, to cross-branch path redirection, to imagining the hot-path cache as a synapse. Thanks to everyone who is willing to help get it right.

---

**Languages:** English · [简体中文](README.zh-CN.md)
