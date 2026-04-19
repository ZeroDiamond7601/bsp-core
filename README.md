# AMXX BSP Navigator Module

**Version:** 1.0
**Author:** Zero
**Platform:** AMX Mod X (GoldSrc Engine)

## Overview

The **BSP Navigator Module** is a high-performance C++ extension for AMX Mod X that provides direct, low-level access to GoldSrc (Half-Life 1 / Counter-Strike 1.6) BSP map data (Version 30). By parsing the BSP file directly in memory, this module allows Pawn plugins to bypass heavy engine calls and perform extremely fast mathematical operations, pathfinding checks, and entity retrievals.

## Features

* **Direct Map Parsing:** Loads and caches BSP lumps (Planes, Nodes, Clipnodes, Leaves, Models, VisData, and Entities) entirely into memory.
* **Fast Collision & Ray Tracing:** Supports exact Hull 0 (Point) and Hull 1-3 (Player sizes) trace lines against the world and specific brush models (`bmodels`).
* **Visibility Engine:** Access raw engine `Vis` and `PAS` (Potentially Audible Set) bitmasks to check if two leaves can see or hear each other.
* **Entity Extraction:** Instantly pull entity origins, brush bounds, and metadata from the map's raw entity lump without relying on engine entity dicts.
* **Ground Detection:** Native implementation for efficiently dropping to the ground from any 3D coordinate.

---

## Installation

1. Compile the `bsp_module.cpp` file using your preferred AMXX module build environment (e.g., GCC or MSVC).
2. Place the compiled binary (e.g., `bsp_navigator_amxx.dll` or `bsp_navigator_amxx_i386.so`) into your `amxmodx/modules/` directory.
3. Add `bsp_navigator` to your `amxmodx/configs/modules.ini` file.
4. Restart your server.

---

## Pawn API (`bsp_navigator.inc`)

To use the module in your AMXX plugins, include the following natives. 

> **Note:** As a best practice in AMXX Pawn programming, avoid using reserved keywords (such as `forward`) for variable names when working with this API.

```pawn
#if defined _bsp_navigator_included
  #endinput
#endif
#define _bsp_navigator_included

/**
 * Hull Types for Tracing
 */
#define HULL_POINT      0  // Standard Point / Bullet Trace
#define HULL_HUMAN      1  // Standing Player
#define HULL_LARGE      2  // Large Hull
#define HULL_HEAD       3  // Crouching Player

/**
 * Loads a BSP map into memory. Must be called before using other natives.
 * * @param mapname   Name of the map (without .bsp extension)
 * @return          1 on success, 0 on failure
 */
native bsp_load_map(const mapname[]);

/**
 * Gets the internal BSP leaf index for a specific 3D coordinate.
 * * @param origin    Float array of the [x, y, z] origin
 * @return          Leaf index, or -1 if outside world
 */
native bsp_get_leaf(const Float:origin[3]);

/**
 * Checks if leaf_b is visible from leaf_a using precomputed BSP VisData.
 * * @param leaf_a    Starting leaf index
 * @param leaf_b    Destination leaf index
 * @return          1 if visible, 0 if occluded
 */
native bsp_check_vis(leaf_a, leaf_b);

/**
 * Checks if leaf_b is within the Potentially Audible Set (PAS) of leaf_a.
 * * @param leaf_a    Starting leaf index
 * @param leaf_b    Destination leaf index
 * @return          1 if audible, 0 otherwise
 */
native bsp_check_pas(leaf_a, leaf_b);

/**
 * Grabs the coordinates of up to `max_found` entities of a specific class.
 * * @param classname Classname to search for (e.g., "info_player_start")
 * @param output    Float array to store the packed origins (size should be max_found * 3)
 * @param max_found Maximum number of entity origins to retrieve
 * @return          Number of entities found
 */
native nav_get_entities(const classname[], Float:output[], max_found);

/**
 * Gets the origin of a specific entity by its index relative to its class.
 * * @param classname     Classname to search for
 * @param target_index  The n-th entity of this class (0-based)
 * @param output        Float array to store the [x, y, z] origin
 * @return              1 on success, 0 on failure
 */
native bsp_get_entity_origin(const classname[], target_index, Float:output[3]);

/**
 * Gets the bounding box (mins/maxs) of a brush entity model.
 * * @param classname     Classname of the brush entity (e.g., "func_wall")
 * @param target_index  The n-th entity of this class (0-based)
 * @param out_mins      Float array to store mins
 * @param out_maxs      Float array to store maxs
 * @return              1 on success, 0 on failure
 */
native bsp_get_brush_model(const classname[], target_index, Float:out_mins[3], Float:out_maxs[3]);

/**
 * Projects a point downwards until it hits the ground geometry (Hull 1).
 * * @param start     Starting Float [x, y, z] origin
 * @param out       Float array to store the ground [x, y, z] origin
 * @return          1 if ground was found, 0 otherwise
 */
native nav_get_ground(const Float:start[3], Float:out[3]);

/**
 * Traces a line against world geometry.
 * * @param start     Starting [x, y, z]
 * @param end       Ending [x, y, z]
 * @param hull_type Collision hull (0-3)
 * @return          1 if a hit occurred, 0 if clear
 */
native nav_trace_wall(const Float:start[3], const Float:end[3], hull_type);

/**
 * Traces a line against a specific Brush Model (bmodel).
 * * @param model_idx BModel index to trace against
 * @param start     Starting [x, y, z]
 * @param end       Ending [x, y, z]
 * @param hull_type Collision hull (0-3)
 * @return          1 if a hit occurred, 0 if clear
 */
native nav_trace_model(model_idx, const Float:start[3], const Float:end[3], hull_type);

/**
 * Retrieves the raw BSP contents (Solid, Water, Empty, etc.) at a given point.
 * * @param origin    Float array of the [x, y, z] origin
 * @return          Contents integer code
 */
native nav_get_contents(const Float:origin[3]);
```

## How It Works Under The Hood

The module operates completely independently of the game engine's live entity system:
1. **`bsp_load_map`**: Opens `cstrike/maps/<mapname>.bsp`. It parses the file headers to locate the 15 standard BSP lumps.
2. **Memory Safety**: Uses safe boundary checks (`MAP_LUMP_SAFE`) to ensure the server doesn't crash if a map is corrupted or improperly compiled.
3. **BSP Node Tree**: Custom native recursive routines (`TraceNode`, `TraceClipnode`) traverse the map's BSP tree directly in memory to resolve geometry calculations up to 10x faster than traditional `engfunc(EngFunc_TraceLine)` calls.
4. **Visibility Checks**: Traverses standard Quake-engine bitwise arrays to execute direct visibility checks, perfect for creating intelligent AI routines or optimizing network culling.

## Contributing

Fork this repository and submit pull requests. Due to the strict alignment requirements (`#pragma pack(push, 1)`) of GoldSrc data structures, ensure any modifications to internal structs carefully respect the 32-bit architectural norms of the HL1 engine.
