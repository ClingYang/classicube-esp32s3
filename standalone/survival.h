/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef STANDALONE_SURVIVAL_H
#define STANDALONE_SURVIVAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SV_REED,
    SV_DRIFTWOOD,
    SV_SHALE,
    SV_RESIN,
    SV_SEED_POD,
    SV_LATTICE,
    SV_LANTERN,
    SV_RESOURCE_COUNT
} SV_Resource;

typedef enum {
    SV_WEDGE,
    SV_MALLET,
    SV_TOOL_COUNT
} SV_Tool;

typedef enum {
    SV_MAKE_WEDGE,
    SV_MAKE_MALLET,
    SV_MAKE_LATTICE,
    SV_MAKE_LANTERN,
    SV_RECIPE_COUNT
} SV_Recipe;

typedef enum {
    SV_GATHER_IDLE,
    SV_GATHER_NEEDS_TOOL,
    SV_GATHER_WORKING,
    SV_GATHER_READY
} SV_GatherResult;

enum {
    SV_MAX_HEALTH = 12,
    SV_MAX_ENERGY = 8,
    SV_STACK_LIMIT = 32,
    SV_DAY_LENGTH_MS = 144000,
    SV_SAVE_SIZE = 32
};

typedef struct {
    uint8_t items[SV_RESOURCE_COUNT];
    uint8_t tools[SV_TOOL_COUNT];
    uint8_t health;
    uint8_t energy;
    uint32_t phase_ms;
    uint16_t exposure_ms;
    uint16_t exertion_ms;
    uint16_t rest_ms;
    uint32_t gather_site;
    uint16_t gather_ms;
    SV_Resource gather_resource;
    bool gather_active;
} SV_State;

void SV_Init(SV_State* state);
/* Restores vitals and clears carried supplies while keeping world time. */
void SV_Respawn(SV_State* state);
const char* SV_ResourceName(SV_Resource resource);
const char* SV_RecipeName(SV_Recipe recipe);
unsigned SV_Add(SV_State* state, SV_Resource resource, unsigned count);
bool SV_Take(SV_State* state, SV_Resource resource, unsigned count);
bool SV_CanCraft(const SV_State* state, SV_Recipe recipe);
bool SV_Craft(SV_State* state, SV_Recipe recipe);
bool SV_EatSeed(SV_State* state);

/* These timers use active play time. Keep sheltered true under a roof or near
 * a working lantern. Large individual elapsed times are capped at 250 ms. */
void SV_Tick(SV_State* state, uint32_t elapsed_ms, bool moving, bool sheltered);
bool SV_IsNight(const SV_State* state);
uint8_t SV_Daylight(const SV_State* state);
void SV_Damage(SV_State* state, unsigned points);
void SV_Land(SV_State* state, float downward_speed);

/* Site is any stable engine key for the block being gathered. Call Hold while
 * the action is pressed and Cancel on release. Commit once the block is
 * removed. Spawn a pickup using ForageDrop and call Add when picked up. */
SV_GatherResult SV_GatherHold(SV_State* state, uint32_t site, SV_Resource surface,
                             uint32_t elapsed_ms);
unsigned SV_GatherPercent(const SV_State* state);
bool SV_GatherCommit(SV_State* state, uint32_t site, SV_Resource surface);
void SV_GatherCancel(SV_State* state);
SV_Resource SV_ForageDrop(SV_Resource surface, uint32_t site, uint32_t world_seed);

/* The 32-byte little-endian record omits transient gathering state. Loading a
 * corrupt, truncated, or unknown-version record leaves state unchanged. */
bool SV_Save(const SV_State* state, uint8_t* data, size_t size);
bool SV_Load(SV_State* state, const uint8_t* data, size_t size);

#endif
