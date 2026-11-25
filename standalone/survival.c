/* SPDX-License-Identifier: GPL-3.0-only */
#include "survival.h"

enum {
    DAY_END_MS = 72000,
    NIGHT_START_MS = 84000,
    NIGHT_END_MS = 128000,
    LOW_LIGHT = 46,
    COLD_INTERVAL_MS = 3700,
    METABOLISM_INTERVAL_MS = 18000,
    RECOVERY_INTERVAL_MS = 5800
};

typedef struct {
    const char* label;
    SV_Resource first;
    SV_Resource second;
    uint8_t first_cost;
    uint8_t second_cost;
    SV_Resource product;
    SV_Tool tool;
    uint8_t produced;
} Recipe;

static const uint8_t durability[SV_TOOL_COUNT] = { 11, 32 };
static const char* const names[SV_RESOURCE_COUNT] = {
    "芦苇", "浮木", "页岩", "树脂", "种荚", "格栅", "灯笼"
};
static const Recipe recipes[SV_RECIPE_COUNT] = {
    { "木楔", SV_DRIFTWOOD, SV_REED, 2, 1, SV_RESOURCE_COUNT, SV_WEDGE, 0 },
    { "页岩锤", SV_SHALE, SV_DRIFTWOOD, 3, 1, SV_RESOURCE_COUNT, SV_MALLET, 0 },
    { "苇格栅", SV_REED, SV_SHALE, 3, 1, SV_LATTICE, SV_TOOL_COUNT, 2 },
    { "树脂灯", SV_RESIN, SV_SHALE, 1, 2, SV_LANTERN, SV_TOOL_COUNT, 1 },
};
static const uint16_t gather_ms[SV_RESOURCE_COUNT] = { 420, 980, 1420, 520, 380, 650, 680 };

static unsigned minimum(unsigned a, unsigned b)
{
    return a < b ? a : b;
}

static bool resource_valid(SV_Resource resource)
{
    return (unsigned)resource < SV_RESOURCE_COUNT;
}

void SV_Init(SV_State* state)
{
    *state = (SV_State){ .health = SV_MAX_HEALTH, .energy = SV_MAX_ENERGY };
}

void SV_Respawn(SV_State* state)
{
    uint32_t phase = state->phase_ms;
    SV_Init(state);
    state->phase_ms = phase;
}

const char* SV_ResourceName(SV_Resource resource)
{
    return resource_valid(resource) ? names[resource] : "";
}

const char* SV_RecipeName(SV_Recipe recipe)
{
    return (unsigned)recipe < SV_RECIPE_COUNT ? recipes[recipe].label : "";
}

unsigned SV_Add(SV_State* state, SV_Resource resource, unsigned count)
{
    if (!state->health || !resource_valid(resource)) return 0;
    unsigned accepted = minimum(count, SV_STACK_LIMIT - state->items[resource]);
    state->items[resource] += (uint8_t)accepted;
    return accepted;
}

bool SV_Take(SV_State* state, SV_Resource resource, unsigned count)
{
    if (!state->health || !resource_valid(resource) || count > state->items[resource]) return false;
    state->items[resource] -= (uint8_t)count;
    return true;
}

bool SV_CanCraft(const SV_State* state, SV_Recipe recipe)
{
    if (!state->health || (unsigned)recipe >= SV_RECIPE_COUNT) return false;
    const Recipe* rule = &recipes[recipe];
    if (state->items[rule->first] < rule->first_cost ||
        state->items[rule->second] < rule->second_cost) return false;

    if (rule->tool < SV_TOOL_COUNT) return state->tools[rule->tool] == 0;
    return state->items[rule->product] + rule->produced <= SV_STACK_LIMIT;
}

bool SV_Craft(SV_State* state, SV_Recipe recipe)
{
    if (!SV_CanCraft(state, recipe)) return false;
    const Recipe* rule = &recipes[recipe];
    state->items[rule->first] -= rule->first_cost;
    state->items[rule->second] -= rule->second_cost;
    if (rule->tool < SV_TOOL_COUNT) {
        state->tools[rule->tool] = durability[rule->tool];
    } else {
        state->items[rule->product] += rule->produced;
    }
    return true;
}

bool SV_EatSeed(SV_State* state)
{
    if ((state->energy == SV_MAX_ENERGY && state->health == SV_MAX_HEALTH) ||
        !SV_Take(state, SV_SEED_POD, 1)) return false;
    state->energy = (uint8_t)minimum(SV_MAX_ENERGY, state->energy + 2);
    if (state->health < SV_MAX_HEALTH) state->health++;
    return true;
}

bool SV_IsNight(const SV_State* state)
{
    return state->phase_ms >= NIGHT_START_MS && state->phase_ms < NIGHT_END_MS;
}

uint8_t SV_Daylight(const SV_State* state)
{
    uint32_t phase = state->phase_ms;
    if (phase < DAY_END_MS) return 255;
    if (phase < NIGHT_START_MS) {
        return (uint8_t)(255 - (255 - LOW_LIGHT) * (phase - DAY_END_MS) /
                         (NIGHT_START_MS - DAY_END_MS));
    }
    if (phase < NIGHT_END_MS) return LOW_LIGHT;
    return (uint8_t)(LOW_LIGHT + (255 - LOW_LIGHT) * (phase - NIGHT_END_MS) /
                     (SV_DAY_LENGTH_MS - NIGHT_END_MS));
}

void SV_Damage(SV_State* state, unsigned points)
{
    if (!points) return;
    state->health -= (uint8_t)minimum(points, state->health);
    state->rest_ms = 0;
    if (!state->health) SV_GatherCancel(state);
}

void SV_Land(SV_State* state, float downward_speed)
{
    if (downward_speed > 14.5f) {
        float impact = (downward_speed - 14.5f) / 3.25f;
        unsigned points = impact >= SV_MAX_HEALTH ? SV_MAX_HEALTH : 1 + (unsigned)impact;
        SV_Damage(state, points);
    }
}

static uint32_t step_ms(uint32_t elapsed_ms)
{
    return elapsed_ms > 250 ? 250 : elapsed_ms;
}

void SV_Tick(SV_State* state, uint32_t elapsed_ms, bool moving, bool sheltered)
{
    uint32_t step = step_ms(elapsed_ms);
    state->phase_ms += step;
    if (state->phase_ms >= SV_DAY_LENGTH_MS) state->phase_ms -= SV_DAY_LENGTH_MS;
    if (!state->health) return;

    bool cold = SV_IsNight(state) && !sheltered;
    if (cold) {
        state->exposure_ms += (uint16_t)step;
        if (state->exposure_ms >= COLD_INTERVAL_MS) {
            state->exposure_ms -= COLD_INTERVAL_MS;
            SV_Damage(state, 1);
        }
    } else {
        state->exposure_ms = 0;
    }

    if (!state->health) return;
    state->exertion_ms += (uint16_t)(step * (moving || state->gather_active ? 2 : 1));
    if (state->exertion_ms >= METABOLISM_INTERVAL_MS) {
        state->exertion_ms -= METABOLISM_INTERVAL_MS;
        if (state->energy) state->energy--;
        else SV_Damage(state, 1);
    }

    if (state->health && state->health < SV_MAX_HEALTH &&
        state->energy >= 6 && !cold && !moving && !state->gather_active) {
        state->rest_ms += (uint16_t)step;
        if (state->rest_ms >= RECOVERY_INTERVAL_MS) {
            state->rest_ms = 0;
            state->health++;
        }
    } else {
        state->rest_ms = 0;
    }
}

/* The mallet is preferred for shale, then the lighter wedge. */
static SV_Tool fitted_tool(const SV_State* state, SV_Resource surface)
{
    if (surface != SV_SHALE) return SV_TOOL_COUNT;
    if (state->tools[SV_MALLET]) return SV_MALLET;
    if (state->tools[SV_WEDGE]) return SV_WEDGE;
    return SV_TOOL_COUNT;
}

static unsigned required_ms(const SV_State* state, SV_Resource surface)
{
    if (surface == SV_SHALE && fitted_tool(state, surface) == SV_MALLET) return 940;
    return gather_ms[surface];
}

void SV_GatherCancel(SV_State* state)
{
    state->gather_active = false;
    state->gather_ms = 0;
}

SV_GatherResult SV_GatherHold(SV_State* state, uint32_t site, SV_Resource surface,
                             uint32_t elapsed_ms)
{
    if (!state->health || !resource_valid(surface)) {
        SV_GatherCancel(state);
        return SV_GATHER_IDLE;
    }
    if (surface == SV_SHALE && fitted_tool(state, surface) == SV_TOOL_COUNT) {
        SV_GatherCancel(state);
        return SV_GATHER_NEEDS_TOOL;
    }

    if (!state->gather_active || site != state->gather_site || surface != state->gather_resource) {
        state->gather_active = true;
        state->gather_site = site;
        state->gather_resource = surface;
        state->gather_ms = 0;
    }
    unsigned target_ms = required_ms(state, surface);
    state->gather_ms = (uint16_t)minimum(target_ms, state->gather_ms + step_ms(elapsed_ms));
    return state->gather_ms >= target_ms ? SV_GATHER_READY : SV_GATHER_WORKING;
}

unsigned SV_GatherPercent(const SV_State* state)
{
    if (!state->gather_active) return 0;
    return minimum(100, 100u * state->gather_ms / required_ms(state, state->gather_resource));
}

bool SV_GatherCommit(SV_State* state, uint32_t site, SV_Resource surface)
{
    if (!state->health || !state->gather_active || site != state->gather_site ||
        surface != state->gather_resource || state->gather_ms < required_ms(state, surface)) return false;

    SV_Tool tool = fitted_tool(state, surface);
    if (surface == SV_SHALE && tool == SV_TOOL_COUNT) return false;
    if (tool < SV_TOOL_COUNT) state->tools[tool]--;
    SV_GatherCancel(state);
    return true;
}

SV_Resource SV_ForageDrop(SV_Resource surface, uint32_t site, uint32_t world_seed)
{
    uint32_t t = site ^ world_seed;
    t = (t ^ (t >> 16)) * 0x7feb352du;
    t = (t ^ (t >> 15)) * 0x846ca68bu;
    t ^= t >> 16;

    if (surface == SV_REED && t % 4 == 0) return SV_SEED_POD;
    if (surface == SV_DRIFTWOOD && t % 5 == 0) return SV_RESIN;
    return surface;
}

_Static_assert(SV_RESOURCE_COUNT == 7 && SV_TOOL_COUNT == 2,
               "Update the save record layout if inventory entries change");

static bool valid_record(const SV_State* state)
{
    if (state->health > SV_MAX_HEALTH || state->energy > SV_MAX_ENERGY ||
        state->phase_ms >= SV_DAY_LENGTH_MS || state->exposure_ms >= COLD_INTERVAL_MS ||
        state->exertion_ms >= METABOLISM_INTERVAL_MS || state->rest_ms >= RECOVERY_INTERVAL_MS)
        return false;

    for (unsigned i = 0; i < SV_RESOURCE_COUNT; i++) {
        if (state->items[i] > SV_STACK_LIMIT) return false;
    }
    for (unsigned i = 0; i < SV_TOOL_COUNT; i++) {
        if (state->tools[i] > durability[i]) return false;
    }
    return true;
}

static uint32_t read_u16(const uint8_t* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t* data)
{
    return read_u16(data) | (read_u16(data + 2) << 16);
}

static void write_u16(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* data, uint32_t value)
{
    write_u16(data, value);
    write_u16(data + 2, value >> 16);
}

static uint32_t record_checksum(const uint8_t* data)
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 28; i++) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

bool SV_Save(const SV_State* state, uint8_t* data, size_t size)
{
    if (!data || size < SV_SAVE_SIZE || !valid_record(state)) return false;
    data[0] = 'S';
    data[1] = 'V';
    data[2] = 'R';
    data[3] = 1;
    for (unsigned i = 0; i < SV_RESOURCE_COUNT; i++) data[4 + i] = state->items[i];
    for (unsigned i = 0; i < SV_TOOL_COUNT; i++) data[11 + i] = state->tools[i];
    data[13] = state->health;
    data[14] = state->energy;
    data[15] = 0;
    write_u32(data + 16, state->phase_ms);
    write_u16(data + 20, state->exposure_ms);
    write_u16(data + 22, state->exertion_ms);
    write_u16(data + 24, state->rest_ms);
    write_u16(data + 26, 0);
    write_u32(data + 28, record_checksum(data));
    return true;
}

bool SV_Load(SV_State* state, const uint8_t* data, size_t size)
{
    if (!data || size != SV_SAVE_SIZE || data[0] != 'S' || data[1] != 'V' || data[2] != 'R' ||
        data[3] != 1 || data[15] || read_u16(data + 26) || read_u32(data + 28) != record_checksum(data))
        return false;

    SV_State restored = { 0 };
    for (unsigned i = 0; i < SV_RESOURCE_COUNT; i++) restored.items[i] = data[4 + i];
    for (unsigned i = 0; i < SV_TOOL_COUNT; i++) restored.tools[i] = data[11 + i];
    restored.health = data[13];
    restored.energy = data[14];
    restored.phase_ms = read_u32(data + 16);
    restored.exposure_ms = (uint16_t)read_u16(data + 20);
    restored.exertion_ms = (uint16_t)read_u16(data + 22);
    restored.rest_ms = (uint16_t)read_u16(data + 24);
    if (!valid_record(&restored)) return false;
    *state = restored;
    return true;
}
