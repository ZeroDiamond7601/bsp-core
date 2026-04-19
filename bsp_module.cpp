// Author: Zero
#include "amxxmodule.h"
#include <stdio.h>
#include <vector>
#include <string>
#include <cstring>
#include <unordered_map>

#pragma pack(push, 1)
#define HEADER_LUMPS 15
struct vec3_t { float x, y, z; };
struct dentry_t { int fileofs, filelen; };
struct dheader_t { int version; dentry_t lumps[HEADER_LUMPS]; };
struct dplane_t { vec3_t normal; float dist; int type; };
struct dnode_t { int planenum; short children[2]; short mins[3], maxs[3]; unsigned short firstface, numfaces; };
struct dclipnode_t { int planenum; short children[2]; };
struct dleaf_t { int contents; int visofs; short mins[3], maxs[3]; unsigned short firstmarksurface, nummarksurfaces; unsigned char ambient_level[4]; };
struct dmodel_t { vec3_t mins, maxs, origin; int headnode[4]; int visleafs; int firstface, numfaces; };
#pragma pack(pop)

// ============================================================================
// Global Memory & Caches
// ============================================================================
std::vector<unsigned char> g_bspdata;

dplane_t* g_planes = NULL;       int g_numplanes = 0;
dnode_t* g_nodes = NULL;         int g_numnodes = 0;
dclipnode_t* g_clipnodes = NULL; int g_numclipnodes = 0;
dleaf_t* g_leaves = NULL;        int g_numleaves = 0;
dmodel_t* g_models = NULL;       int g_nummodels = 0;
unsigned char* g_visdata = NULL; int g_visdatalen = 0;

struct MapEntity {
    std::string classname;
    std::unordered_map<std::string, std::string> keyvalues;
};
std::vector<MapEntity> g_Entities;

// ============================================================================
// Internal Engine Logic
// ============================================================================

void ParseMapEntities(const char* entString, int length) {
    g_Entities.clear();
    std::string data(entString, length);
    size_t pos = 0;

    while ((pos = data.find('{', pos)) != std::string::npos) {
        size_t end = data.find('}', pos);
        if (end == std::string::npos) break;
        
        std::string entData = data.substr(pos + 1, end - pos - 1);
        MapEntity ent;
        size_t q1 = 0;

        while ((q1 = entData.find('"', q1)) != std::string::npos) {
            size_t q2 = entData.find('"', q1 + 1); if (q2 == std::string::npos) break;
            size_t q3 = entData.find('"', q2 + 1); if (q3 == std::string::npos) break;
            size_t q4 = entData.find('"', q3 + 1); if (q4 == std::string::npos) break;
            
            std::string key = entData.substr(q1 + 1, q2 - q1 - 1);
            std::string val = entData.substr(q3 + 1, q4 - q3 - 1);
            ent.keyvalues[key] = val;
            
            if (key == "classname") ent.classname = val;
            q1 = q4 + 1;
        }
        
        if (!ent.classname.empty()) g_Entities.push_back(ent);
        pos = end + 1;
    }
}

// Traces HULL 0 (Point/Bullets)
bool TraceNode(int num, float p1f, float p2f, float* p1, float* p2) {
    if (num < 0) return (num == -2); 
    if (num >= g_numnodes) return false; 
    
    dnode_t *node = &g_nodes[num];
    if (node->planenum >= g_numplanes) return false; 
    dplane_t *plane = &g_planes[node->planenum];

    float t1 = (plane->normal.x * p1[0] + plane->normal.y * p1[1] + plane->normal.z * p1[2]) - plane->dist;
    float t2 = (plane->normal.x * p2[0] + plane->normal.y * p2[1] + plane->normal.z * p2[2]) - plane->dist;

    if (t1 >= 0 && t2 >= 0) return TraceNode(node->children[0], p1f, p2f, p1, p2);
    if (t1 < 0 && t2 < 0) return TraceNode(node->children[1], p1f, p2f, p1, p2);

    float frac = t1 / (t1 - t2);
    float mid[3] = { p1[0] + frac * (p2[0] - p1[0]), p1[1] + frac * (p2[1] - p1[1]), p1[2] + frac * (p2[2] - p1[2]) };

    return TraceNode(node->children[t1 < 0], p1f, p1f + (p2f - p1f) * frac, p1, mid) ||
           TraceNode(node->children[t1 >= 0], p1f + (p2f - p1f) * frac, p2f, mid, p2);
}

// Traces HULL 1, 2, 3 (Player Sizes)
bool TraceClipnode(int num, float p1f, float p2f, float* p1, float* p2) {
    if (num < 0) return (num == -2); 
    if (num >= g_numclipnodes) return false; 
    
    dclipnode_t *node = &g_clipnodes[num];
    if (node->planenum >= g_numplanes) return false; 
    dplane_t *plane = &g_planes[node->planenum];

    float t1 = (plane->normal.x * p1[0] + plane->normal.y * p1[1] + plane->normal.z * p1[2]) - plane->dist;
    float t2 = (plane->normal.x * p2[0] + plane->normal.y * p2[1] + plane->normal.z * p2[2]) - plane->dist;

    if (t1 >= 0 && t2 >= 0) return TraceClipnode(node->children[0], p1f, p2f, p1, p2);
    if (t1 < 0 && t2 < 0) return TraceClipnode(node->children[1], p1f, p2f, p1, p2);

    float frac = t1 / (t1 - t2);
    float mid[3] = { p1[0] + frac * (p2[0] - p1[0]), p1[1] + frac * (p2[1] - p1[1]), p1[2] + frac * (p2[2] - p1[2]) };

    return TraceClipnode(node->children[t1 < 0], p1f, p1f + (p2f - p1f) * frac, p1, mid) ||
           TraceClipnode(node->children[t1 >= 0], p1f + (p2f - p1f) * frac, p2f, mid, p2);
}

bool EngineTraceHull(int headnode_index, int hull_type, float* start, float* end) {
    if (hull_type == 0) return TraceNode(headnode_index, 0.0f, 1.0f, start, end);
    return TraceClipnode(headnode_index, 0.0f, 1.0f, start, end);
}

int GetLeafIDAtPoint(float origin[3]) {
    if (g_numnodes <= 0 || !g_nodes) return -1;
    int index = 0;
    while (index >= 0) {
        if (index >= g_numnodes) return -1;
        dnode_t *node = &g_nodes[index];
        dplane_t *plane = &g_planes[node->planenum];
        float dot = (plane->normal.x * origin[0] + plane->normal.y * origin[1] + plane->normal.z * origin[2]) - plane->dist;
        index = (dot >= 0) ? node->children[0] : node->children[1];
    }
    return -index - 1;
}

bool CheckBitBuffer(int offset, int dest_leaf) {
    if (!g_visdata || g_visdatalen == 0 || offset < 0 || offset >= g_visdatalen) return true;
    int current_leaf = 1;
    const unsigned char *v = g_visdata + offset;
    while (current_leaf < g_numleaves) {
        if (v[0] == 0) {
            current_leaf += 8 * v[1]; v += 2;
        } else {
            for (int bit = 1; bit <= 8; bit++) {
                if (current_leaf == dest_leaf) return (v[0] & (1 << (bit - 1))) != 0;
                current_leaf++;
            }
            v++;
        }
    }
    return false;
}

// ============================================================================
// AMXX Native Functions
// ============================================================================

static cell AMX_NATIVE_CALL bsp_load_map(AMX *amx, cell *params) {
    int len;
    char *mapname = MF_GetAmxString(amx, params[1], 1, &len);
    if (!mapname) return 0;
    
    char path[256];
    snprintf(path, sizeof(path), "cstrike/maps/%s.bsp", mapname);

    FILE *f = fopen(path, "rb");
    if (!f) { MF_Log("[BSP ERROR] -> Could not open file."); return 0; }

    fseek(f, 0, SEEK_END);
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (filesize < (long)sizeof(dheader_t)) { fclose(f); return 0; }

    g_bspdata.clear();
    g_Entities.clear();
    g_bspdata.resize(filesize);
    fread(g_bspdata.data(), 1, filesize, f);
    fclose(f);

    dheader_t* header = (dheader_t*)g_bspdata.data();
    if (header->version != 30) { g_bspdata.clear(); return 0; }

    #define MAP_LUMP_SAFE(type, ptr, count, index, name) \
        if (header->lumps[index].fileofs + header->lumps[index].filelen > filesize) { \
            MF_Log("[BSP FATAL] -> Lump %d extends past end of file!", index); \
            g_bspdata.clear(); return 0; \
        } \
        ptr = (type*)(g_bspdata.data() + header->lumps[index].fileofs); \
        count = header->lumps[index].filelen / sizeof(type);

    MAP_LUMP_SAFE(dplane_t, g_planes, g_numplanes, 1, "Planes");
    MAP_LUMP_SAFE(dnode_t, g_nodes, g_numnodes, 5, "Nodes");
    MAP_LUMP_SAFE(dclipnode_t, g_clipnodes, g_numclipnodes, 9, "Clipnodes");
    MAP_LUMP_SAFE(dleaf_t, g_leaves, g_numleaves, 10, "Leaves");
    MAP_LUMP_SAFE(dmodel_t, g_models, g_nummodels, 14, "Models");

    if (header->lumps[4].fileofs + header->lumps[4].filelen <= filesize) {
        g_visdata = g_bspdata.data() + header->lumps[4].fileofs;
        g_visdatalen = header->lumps[4].filelen;
    }

    if (header->lumps[0].filelen > 0 && header->lumps[0].fileofs + header->lumps[0].filelen <= filesize) {
        ParseMapEntities((char*)(g_bspdata.data() + header->lumps[0].fileofs), header->lumps[0].filelen);
    }

    MF_Log("[BSP SUCCESS] -> Map '%s' loaded. Engine ready. Parsed %d entities.", mapname, (int)g_Entities.size());
    return 1;
}

static cell AMX_NATIVE_CALL bsp_get_entity_origin(AMX *amx, cell *params) {
    int len;
    char* target_class = MF_GetAmxString(amx, params[2], 1, &len);
    int target_index = params[3];
    cell* output = MF_GetAmxAddr(amx, params[4]);
    
    int current_index = 0;
    for (const auto& ent : g_Entities) {
        if (ent.classname == target_class) {
            if (current_index == target_index) {
                auto it = ent.keyvalues.find("origin");
                if (it != ent.keyvalues.end()) {
                    float x, y, z;
                    if (sscanf(it->second.c_str(), "%f %f %f", &x, &y, &z) == 3) {
                        output[0] = amx_ftoc(x); output[1] = amx_ftoc(y); output[2] = amx_ftoc(z);
                        return 1;
                    }
                }
                return 0; 
            }
            current_index++;
        }
    }
    return 0;
}

static cell AMX_NATIVE_CALL bsp_get_brush_model(AMX *amx, cell *params) {
    if (g_nummodels <= 0) return 0;
    int len;
    char* target_class = MF_GetAmxString(amx, params[2], 1, &len);
    int target_index = params[3];
    cell* out_mins = MF_GetAmxAddr(amx, params[4]);
    cell* out_maxs = MF_GetAmxAddr(amx, params[5]);
    
    int current_index = 0;
    for (const auto& ent : g_Entities) {
        if (ent.classname == target_class) {
            if (current_index == target_index) {
                auto it = ent.keyvalues.find("model");
                if (it != ent.keyvalues.end() && it->second[0] == '*') {
                    int model_idx = atoi(it->second.c_str() + 1);
                    if (model_idx >= 0 && model_idx < g_nummodels) {
                        dmodel_t* mod = &g_models[model_idx];
                        out_mins[0] = amx_ftoc(mod->mins.x); out_mins[1] = amx_ftoc(mod->mins.y); out_mins[2] = amx_ftoc(mod->mins.z);
                        out_maxs[0] = amx_ftoc(mod->maxs.x); out_maxs[1] = amx_ftoc(mod->maxs.y); out_maxs[2] = amx_ftoc(mod->maxs.z);
                        return 1;
                    }
                }
                return 0;
            }
            current_index++;
        }
    }
    return 0;
}

static cell AMX_NATIVE_CALL nav_get_entities(AMX *amx, cell *params) {
    int len;
    char* target_class = MF_GetAmxString(amx, params[1], 1, &len);
    cell* output = MF_GetAmxAddr(amx, params[2]);
    int max_found = params[3];
    int count = 0;

    for (const auto& ent : g_Entities) {
        if (ent.classname == target_class && count < max_found) {
            auto it = ent.keyvalues.find("origin");
            if (it != ent.keyvalues.end()) {
                float x, y, z;
                if (sscanf(it->second.c_str(), "%f %f %f", &x, &y, &z) == 3) {
                    output[count * 3 + 0] = amx_ftoc(x);
                    output[count * 3 + 1] = amx_ftoc(y);
                    output[count * 3 + 2] = amx_ftoc(z);
                    count++;
                }
            }
        }
    }
    return count;
}

static cell AMX_NATIVE_CALL nav_trace_wall(AMX *amx, cell *params) {
    if (g_nummodels <= 0 || !g_models) return 0;
    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    float start[3] = { amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]) };
    float end[3] = { amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]) };
    int hull_type = params[3];
    if (hull_type < 0 || hull_type > 3) return 0;

    return (cell)EngineTraceHull(g_models[0].headnode[hull_type], hull_type, start, end);
}

static cell AMX_NATIVE_CALL nav_trace_model(AMX *amx, cell *params) {
    int idx = params[1];
    if (idx < 0 || idx >= g_nummodels) return 0;
    cell *c_s = MF_GetAmxAddr(amx, params[2]);
    cell *c_e = MF_GetAmxAddr(amx, params[3]);
    float start[3] = { amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]) };
    float end[3] = { amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]) };
    int hull_type = params[4];
    if (hull_type < 0 || hull_type > 3) return 0;

    return (cell)EngineTraceHull(g_models[idx].headnode[hull_type], hull_type, start, end);
}

static cell AMX_NATIVE_CALL nav_get_ground(AMX *amx, cell *params) {
    if (g_nummodels <= 0 || !g_models) return 0;
    cell* c_start = MF_GetAmxAddr(amx, params[1]);
    cell* c_out = MF_GetAmxAddr(amx, params[2]);
    float start[3] = { amx_ctof(c_start[0]), amx_ctof(c_start[1]), amx_ctof(c_start[2]) };
    
    int headnode = g_models[0].headnode[1]; 
    for(float d = 0; d < 2000.0f; d += 5.0f) {
        float p1[3] = {start[0], start[1], start[2] - d};
        float p2[3] = {start[0], start[1], start[2] - d - 5.0f};
        if (EngineTraceHull(headnode, 1, p1, p2)) {
            c_out[0] = amx_ftoc(p1[0]); c_out[1] = amx_ftoc(p1[1]); c_out[2] = amx_ftoc(p1[2]);
            return 1;
        }
    }
    return 0;
}

static cell AMX_NATIVE_CALL bsp_get_leaf(AMX *amx, cell *params) {
    cell *ptr = MF_GetAmxAddr(amx, params[1]);
    float origin[3] = { amx_ctof(ptr[0]), amx_ctof(ptr[1]), amx_ctof(ptr[2]) };
    return (cell)GetLeafIDAtPoint(origin);
}

static cell AMX_NATIVE_CALL bsp_check_vis(AMX *amx, cell *params) {
    int leaf_a = params[1];
    if (leaf_a < 0 || leaf_a >= g_numleaves) return 1;
    return (cell)CheckBitBuffer(g_leaves[leaf_a].visofs, params[2]);
}

static cell AMX_NATIVE_CALL bsp_check_pas(AMX *amx, cell *params) {
    int leaf_a = params[1];
    if (leaf_a < 0 || leaf_a >= g_numleaves) return 1;
    int pas_offset = g_leaves[leaf_a].visofs + (g_numleaves + 7) / 8;
    return (cell)CheckBitBuffer(pas_offset, params[2]);
}

static cell AMX_NATIVE_CALL nav_get_contents(AMX *amx, cell *params) {
    if (g_nummodels <= 0 || !g_models) return 0;
    cell *ptr = MF_GetAmxAddr(amx, params[1]);
    float p[3] = { amx_ctof(ptr[0]), amx_ctof(ptr[1]), amx_ctof(ptr[2]) };
    
    int index = g_models[0].headnode[0];
    while (index >= 0) {
        if (index >= g_numclipnodes) break;
        dclipnode_t *node = &g_clipnodes[index];
        dplane_t *plane = &g_planes[node->planenum];
        float d = (plane->normal.x * p[0] + plane->normal.y * p[1] + plane->normal.z * p[2]) - plane->dist;
        index = (d >= 0) ? node->children[0] : node->children[1];
    }
    return (cell)index; 
}

// ============================================================================
// Module Registration
// ============================================================================

AMX_NATIVE_INFO bsp_Natives[] = {
    {"bsp_load_map", bsp_load_map},
    {"bsp_get_leaf", bsp_get_leaf},
    {"bsp_check_vis", bsp_check_vis},
    {"bsp_check_pas", bsp_check_pas},
    {"nav_get_entities", nav_get_entities},
    {"bsp_get_entity_origin", bsp_get_entity_origin},
    {"bsp_get_brush_model", bsp_get_brush_model},
    {"nav_get_ground", nav_get_ground},
    {"nav_trace_wall", nav_trace_wall},
    {"nav_trace_model", nav_trace_model},
    {"nav_get_contents", nav_get_contents}, 
    {NULL, NULL}
};

void OnAmxxAttach() { 
    MF_AddNatives(bsp_Natives); 
}

void OnAmxxDetach() { 
    g_bspdata.clear(); 
    g_Entities.clear(); 
}