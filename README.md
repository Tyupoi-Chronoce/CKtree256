# CKtree256

An experimental **256-way byte-radix tree**. The long-term goal is a persistent, cross-process, dangling-pointer-free way to reference nodes inside and across branches: use **logical byte paths** instead of raw pointers, and a **hot-path cache** to flatten cross-branch jumps to O(1).

**Languages:** English · [简体中文](README.zh-CN.md)

---

## Overview

The long-term design is described in `introduction.md` (kept local, not published; see `.gitignore`). This repository currently contains:

- `classBasic.cpp`: the single-file development implementation; the smoke test includes it directly;
- `smoke.cpp`: the DSH-managed public-API smoke test;
- `testMain.cpp`: the owner's scratch entry point, intentionally left as a minimal compilable stub;
- `_4lib_work/CKtree256.hpp` + `.cpp`: the current **library-format split**, generated from `classBasic.cpp` (2026-11-16);
- `lib/libCKtree256.a` + `lib/CKtree256.hpp`: the prebuilt MinGW-w64 static library and matching header;
- `docs/C_API_design.md`: a design draft for a future C ABI (not implemented);
- `docs/threading_design_notes.md`: cautions for the planned background cache-eviction thread.

> **Status: pre-alpha, structural prototype.**
> The tree can create child-pointer arrays, read/write/clear `dataP` along byte paths, link one existing node to another by logical path, resolve those links by source path with `get_linkNode(path, len)`, and recursively free a subtree. HVcache has target-side jump-count threshold/free-slot admission, per-slot generation checks, and a private `_cache_eviction` sweep/penalty/least-count fallback; the eviction helper is not wired into admission or a worker yet, and there is no pinning.
> **Not implemented yet:** cache-eviction wiring and background worker, pinning, a stable public node handle/identity, path validation after structural changes, persistence, a formal test framework, or a build system. `get_linkNode` now accepts a path and returns a `treeNode*`; that pointer is an internal node address, not a stable handle, and is only valid while the node is alive.
> The API will change significantly. **Do not use this in production.**

## Repository layout

| File / directory | Description |
|---|---|
| `classBasic.cpp` | Single-file development implementation (header-style .cpp, no `main`, no include guard); `smoke.cpp` includes it directly |
| `smoke.cpp` | DSH-managed smoke test for the current public API (includes `classBasic.cpp`, does not link the library) |
| `testMain.cpp` | Owner-maintained experimental entry point; DSH no longer edits it |
| `_4lib_work/CKtree256.hpp` | Public header of the library-format split (current API) |
| `_4lib_work/CKtree256.cpp` | Out-of-class implementation of the library split (current API) |
| `lib/libCKtree256.a` | Static library built from the current split (C++ ABI; GCC 14.2.0 / MinGW-w64) |
| `lib/CKtree256.hpp` | Public header shipped with the static library (copy of `_4lib_work/CKtree256.hpp`) |
| `docs/C_API_design.md` | C ABI design draft; not implemented |
| `docs/threading_design_notes.md` | Lifecycle, locking and lifetime cautions for the planned cache-eviction thread |
| `introduction.md` | Original design discussion, kept locally and not committed |
| `README.zh-CN.md` | Chinese version of this README |
| `LICENSE` | MIT license |

> Maintenance relationship: `classBasic.cpp` is the development source. `_4lib_work` is a mechanical split of that source with the same public API, and `lib` is built from `_4lib_work`. After changing `classBasic.cpp`, regenerate the split and rebuild the library with the commands below; `smoke.cpp` includes the source directly and does not link the library. Both the source and the split build warning-free with GCC 14.2.0 / MinGW-w64; the split and `lib/` were refreshed on 2026-11-16.

## What works today

- `createChildren(path, len)` — allocate a 256-slot child-pointer array for an existing node (idempotent; safely returns false when the path is missing)
- `put_data(path, len, dataP)` — ensure the node exists along the path and write `dataP`; writing the same path again only overwrites the pointer and does not free the old data; `dataP` must be non-null and the tree does not own it
- `get_data(path, len)` — read `dataP` by path; returns `nullptr` when the path is missing or the node has no data; `length == 0` reads the root
- `erase_data(path, len)` — clear `dataP` on an existing node without deleting the node; returns false when the path is missing
- `link_node(dstPath, dstLen, targetPath, targetLen)` — link an existing destination node to an existing target node by logical path; repeated calls replace the old path; both endpoints must already exist. The target path is copied with a throwing `new`, so allocation failure propagates as `std::bad_alloc`; like `createChildren` / `put_data`, this public `bool` API can throw.
- `get_linkNode(path, len)` — resolve the link stored on the node at `path`; `length == 0` addresses the root as the source node. On a miss it walks the stored target path, and once the **target's** `hitCounts_4beJumped` passes the threshold (`minCounts_move2cache = 0x0f`) it admits that target to HVcache. Every successful resolution increments the target's counter (saturating at `UINT32_MAX`), not the source's. On a cache hit it validates the slot index, generation and target flags, then returns the cached target's `treeNode*` (not a pointer into the slot table). A stale slot is discarded and the path is used again. Missing source, missing target or no link -> `nullptr`. The returned pointer is only valid while the target node is alive.
- `free_nodeTree(path, len)` — recursively release a subtree, each node's copied external path and its own cache slot; it does **not** free `dataP` and refuses to release the root; missing paths return false
- Internal helpers such as `_mount_node`, `_find_node`, `_create_childrenArray`, `_free_nodeTree`, `_can_putInto_cache`, `_put2cache`, `_get_linkNode`, `_cache_eviction`, `_remove_self_from_cache` and `_forget_link_cache` — the caller must hold the lock; the path-based `_put2cache(path, len)` only resolves the source node and delegates to `_put2cache(treeNode**, ...)`; `_cache_eviction` currently has no caller
- `HVcache_nodeP` / `HVcache_aliveState` / `HVcache_generation` — a non-owning pointer table with `size + 1` slots; slot 0 is a sentinel. `_can_putInto_cache` linearly scans for an already-cached target first, then takes the first free slot, and reports whether the entry is new. `_cache_eviction` can sweep cold slots, apply an eviction penalty, or evict the least-count node when the table is full, but nothing calls it yet.
- `privateMutex` — protects the tree and cache; public entry points take it once per call, private helpers require the caller to hold it

## Library-format split

The repository contains a library-format split of the current source:

```text
_4lib_work/CKtree256.hpp      # public header, class declaration only
_4lib_work/CKtree256.cpp      # out-of-class implementation
lib/CKtree256.hpp             # header shipped with the library
lib/libCKtree256.a            # static library built from the split
```

Rebuild after changing `classBasic.cpp`:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -c _4lib_work/CKtree256.cpp -o CKtree256.o
ar rcs lib/libCKtree256.a CKtree256.o
cp _4lib_work/CKtree256.hpp lib/CKtree256.hpp
```

Link a consumer:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Ilib app.cpp lib/libCKtree256.a -o app
```

The split was regenerated from the current `classBasic.cpp` on 2026-11-16 and exposes the current class API (`createChildren`, `put_data`, `get_data`, `erase_data`, `link_node(path, path)`, `get_linkNode(path, len)`, `free_nodeTree`, destructor). A throwaway consumer compiled with the command above printed `consumer OK sizeof(Tree256)=64 link/get_linkNode OK`; it exercised root/key `put_data`, `link_node`, path-based `get_linkNode` with 20 repeated jumps and a root relink, and `free_nodeTree`. `smoke.cpp` covers `get_data`, `erase_data` and `createChildren` as well.

Only a C++ class interface is provided; there is no C-style / `extern "C"` header yet. `Tree256` depends on `std::mutex`, and `get_linkNode` returns a raw `treeNode*` that is not a stable handle. A future C ABI would need a separate opaque-handle wrapper layer and a lifetime rule for returned nodes; see `docs/C_API_design.md`.

## Build and run

Single-file smoke test (includes `classBasic.cpp` directly):

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic smoke.cpp -o smoke
./smoke
```

Expected output:

```text
[smoke] put_data / get_data / erase_data / createChildren / link_node / get_linkNode / free_nodeTree OK
[smoke] target-side hitCounts_4beJumped admission, cache hit, relink and cached-target free OK
```

The current version builds with zero warnings under GCC 14.2.0 / MinGW-w64, and the smoke test exits with code 0. `testMain.cpp` is the owner's scratch file; its current stub also compiles, but it is not part of the smoke coverage.

## TODO / known issues

Ordered roughly by priority; contributions are welcome:

1. **API not finalized**: the public API has `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode` and `free_nodeTree`. The link path is now usable end-to-end, but there is still no public stable node handle/identity: `get_linkNode` returns a raw `treeNode*` that is only valid while the target is alive. `free_nodeTree` frees structure and cache slots, but the data deleter and single-node-vs-subtree semantics still need to be finalized.
2. **Key and data contract**: explicit lengths support embedded 0, but the external-path format still needs a length or terminator convention, and `dataP` ownership (currently: the tree never deletes it; the caller owns it) needs to be documented as a hard contract. `get_data` cannot currently distinguish "path missing" from "node exists but has no data".
3. **Cache is not finished**: the target-side threshold and generation checks are wired into `get_linkNode`. A private `_cache_eviction` implements sweep, a penalty, and a least-count fallback, but no caller wires it into admission and there is no background worker or pinning (a cached node must not be deleted). `_can_putInto_cache` linearly scans the slot table and takes the first free slot; the path-based `_put2cache` overload only resolves the source and delegates to the pointer overload, so admission logic lives in one place. A cache hit returns the cached target's node pointer, not a stable handle.
4. **Cross-branch references are only half done**: the link stores a copied logical path, but there is no path validation. After the structure changes, an old path can silently point to a different node; path hashing or a global revision is needed. A public node identity/handle is also missing, and the `treeNode*` returned by `get_linkNode` can dangle after `free_nodeTree`.
5. **Child representation wastes memory**: a fixed 256 pointers per node = 2 KB, which is very wasteful for sparse trees. A planned fix is a bitmap + compact array, or ART-style Node4/16/48/256.
6. **`tempNodeP_4easyFind` implicit state**: `_node_isExist` writes the result through a member variable and `_find_node` reads and clears it; a failure or early return can leave a stale value, and the duplicate traversal in `_find_node` is effectively unreachable. Return the slot pointer directly instead of going through a member variable.
7. **Thread safety is unverified**: `privateMutex` is wired into `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, `free_nodeTree` and the destructor; private helpers require the caller to hold the lock. There is no concurrency test, and lock granularity is undecided. A background eviction thread is planned but not implemented; see `docs/threading_design_notes.md` for the lifecycle and lock-order constraints.
8. **Persistence cannot "just dump" yet**: `childFP` is a raw pointer; before it goes cross-process or to disk it must become an arena + offsets, or be serialized as node IDs.
9. **No formal tests or CI**: `smoke.cpp` covers the current public API including `link_node` / `get_linkNode`, repeated jumps, relink and cached-target free; `testMain.cpp` is the owner's scratch file. There is no unit-test framework, CMake, or GitHub Actions, and `_cache_eviction` has no direct test because it has no caller.
10. **Recursive free depth = key length**: the author is aware of and accepts that very deep keys may overflow the stack; this can later be converted to an explicit stack.
11. **Naming legacy**: `size0f_HVcache`, `length_externaRelatedlLink_nodePath`, `self_cacheIndex`, and similar spelling/style issues need to be unified.
12. **Exception safety**: `_mount_node` / `_create_childrenArray` and `_link_node` all use throwing `new`, so a public `bool` call may throw `std::bad_alloc` (documented in the header); callers that need a no-throw contract must catch it. Other failure paths return `false`.
13. **Library artifacts can drift**: `_4lib_work` is a manual split of `classBasic.cpp`, and `lib` is prebuilt for GCC 14.2.0 / MinGW-w64. Re-run the build commands after source changes, and rebuild on other ABIs instead of reusing this archive.

## Design notes

- 256-way fanout = consume one byte per level; tree depth equals the key's byte length
- A cross-branch reference stores the target's **byte path**, not a physical pointer, avoiding dangling pointers
- Once a target's `hitCounts_4beJumped` passes a threshold, the target's physical pointer is placed in an in-tree cache slot to make the jump O(1)
- The cache is **non-owning**: a node placed in the cache must not be deleted directly; `_cache_eviction` exists but is not wired in, and the pinning protocol is not implemented yet
- Slot entries have a generation; a cache hit must match both the source's recorded generation and the target's self-cache flags, otherwise the source falls back to the path
- `get_linkNode` returns the target node itself, not a slot address; the pointer is only valid while that node is alive and is not a stable cross-process handle
- The library keeps a C++ class interface; a C ABI would need a separate opaque-handle wrapper layer and is intentionally not provided yet

## Threading and locking

- `privateMutex` protects the root pointer, tree structure, and HVcache slot table.
- Public entry points that lock internally: `createChildren`, `put_data`, `get_data`, `erase_data`, `link_node`, `get_linkNode`, `free_nodeTree`, `~Tree256`. `link_node` may throw `std::bad_alloc` while holding the lock; RAII releases it during unwinding. `get_linkNode` returns a raw node pointer after unlocking, so its lifetime is the caller's responsibility.
- Private helpers that require the caller to hold the lock: `_find_node`, `_node_isExist`, `_mount_node`, `_create_childrenArray`, `_can_putInto_cache`, `_remove_self_from_cache`, `_forget_link_cache`, `_put2cache`, `_get_linkNode`, `_cache_eviction`, `_find_from_cache`, `_free_nodeTree` (the destructor holds the lock while recursing into `_free_nodeTree`; the recursion itself does not lock again).
- `free_nodeTree` refuses to release the root; public entry points take the lock once per call and do not call an already-locking public function.
- No background thread runs today. If one is added for `_cache_eviction`, start it after construction completes, stop and join it before the destructor takes `privateMutex`, and never call already-locking public methods from the worker; the full checklist is in `docs/threading_design_notes.md`.

## Contributing

At this stage, **design discussion** matters more than code volume. Issues about node layout, cache policy, and persistence are welcome. Before opening a PR, please explain the problem it solves.

## License

[MIT](LICENSE), Copyright (c) 2026 Tyupoi-Chronoce. See [LICENSE](LICENSE) for the full text.

## Acknowledgements

The project started as a discussion about "path as coordinate" — from `0xCC` mojibake, to cross-branch path redirection, to imagining the hot-path cache as a synapse. Thanks to everyone who is willing to help get it right.

---

**Languages:** English · [简体中文](README.zh-CN.md)
