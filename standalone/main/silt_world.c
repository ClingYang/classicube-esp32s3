// SPDX-License-Identifier: GPL-3.0-only
#include "silt_world.h"

#include <stdint.h>
#include <string.h>

#include "BlockID.h"
#include "World.h"

static uint32_t world_seed;

static int distance(int a, int b)
{
    return a > b ? a - b : b - a;
}

static int bound(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static uint32_t sample(int x, int z, uint32_t layer)
{
    uint32_t value = world_seed + layer * UINT32_C(0x45D9F3B);
    value ^= (uint32_t)x * UINT32_C(0x9E3779B9);
    value ^= (uint32_t)z * UINT32_C(0x85EBCA77);
    value ^= value >> 16;
    value *= UINT32_C(0x7FEB352D);
    value ^= value >> 15;
    value *= UINT32_C(0x846CA68B);
    return value ^ (value >> 16);
}

static int mix(int left, int right, int ratio)
{
    return left + (right - left) * ratio / 256;
}

/* Smooth interpolation between independent silt-height samples. */
static int field(int x, int z, int scale, uint32_t layer)
{
    int gx = x / scale;
    int gz = z / scale;
    int tx = x % scale * 256 / scale;
    int tz = z % scale * 256 / scale;
    tx = tx * tx * (768 - tx * 2) >> 16;
    tz = tz * tz * (768 - tz * 2) >> 16;
    int a = (int)(sample(gx, gz, layer) & 255u);
    int b = (int)(sample(gx + 1, gz, layer) & 255u);
    int c = (int)(sample(gx, gz + 1, layer) & 255u);
    int d = (int)(sample(gx + 1, gz + 1, layer) & 255u);
    return mix(mix(a, b, tx), mix(c, d, tx), tz) - 128;
}

static int delta_waterline(void)
{
    return World.Height / 2 - 2;
}

static int delta_height(int x, int z)
{
    int water = delta_waterline();
    int base = water + 1 + field(x, z, 18, 1) / 48 + field(x, z, 7, 2) / 64;
    int left = World.Width / 3 + field(18, z, 23, 3) * World.Width / 1600;
    int right = World.Width * 7 / 10 + field(34, z, 17, 4) * World.Width / 1792;
    int tributary = World.Length / 3 + field(x, 21, 20, 5) * World.Length / 1664;
    int left_dist = distance(x, left);
    int right_dist = distance(x, right);
    int side_dist = distance(z, tributary);
    if (left_dist <= 4) {
        base = water - 2 - (4 - left_dist) / 3;
    } else if (right_dist <= 2 || (x < left && side_dist <= 2)) {
        base = water - 1 - (2 - (right_dist <= 2 ? right_dist : side_dist)) / 2;
    } else if (left_dist == 5 || right_dist == 3 || (x < left && side_dist == 3)) {
        base = water;
    }

    int center_x = distance(x, World.Width / 2);
    int center_z = distance(z, World.Length / 2);
    if (center_x <= 6 && center_z <= 6) {
        /* A dry stepped ridge that always has a clear centre for spawning. */
        int bank = water + 2 + (center_x <= 2 && center_z <= 2);
        if (base < bank) base = bank;
    }
    return bound(base, 2, World.MaxY - 4);
}

static BlockRaw surface_block(int x, int y, int z)
{
    int center_x = distance(x, World.Width / 2);
    int center_z = distance(z, World.Length / 2);
    if (center_x >= 3 && center_x <= 5 && center_z <= 2 &&
        (sample(x, z, 7) & 3u) != 0) return BLOCK_STONE;
    if (y <= delta_waterline()) {
        return (sample(x, z, 8) & 7u) < 3 ? BLOCK_GRAVEL : BLOCK_SAND;
    }
    return BLOCK_GRASS;
}

static void set_if_air(int x, int y, int z, BlockRaw value)
{
    if (!World_Contains(x, y, z)) return;

    int index = World_Pack(x, y, z);
    if (Gen_Blocks[index] == BLOCK_AIR) Gen_Blocks[index] = value;
}

static void place_driftwood(int x, int z, int height, uint32_t shape)
{
    if (!World_ContainsXZ(x, z) || height <= delta_waterline()) return;

    int direction = (shape & 1u) ? 1 : -1;
    int trunk = 2 + (int)((shape >> 1) & 1u);
    for (int i = 1; i <= trunk; i++) {
        set_if_air(x, height + i, z, BLOCK_LOG);
    }
    set_if_air(x + direction, height + trunk, z, BLOCK_LOG);
    set_if_air(x, height + trunk + 1, z, BLOCK_LEAVES);
    set_if_air(x + direction, height + trunk + 1, z, BLOCK_LEAVES);
    set_if_air(x + direction * 2, height + trunk, z, BLOCK_LEAVES);
    set_if_air(x + direction, height + trunk, z + 1, BLOCK_LEAVES);
    set_if_air(x + direction, height + trunk, z - 1, BLOCK_LEAVES);
    set_if_air(x - direction, height + trunk, z, BLOCK_LEAVES);
}

static cc_bool delta_prepare(int seed)
{
    world_seed = (uint32_t)seed;
    return World.Width >= 16 && World.Length >= 16 && World.Height >= 12;
}

static void delta_generate(void)
{
    Gen_CurrentProgress = 0.0f;
    Gen_CurrentState = "Charting the delta";
    memset(Gen_Blocks, BLOCK_AIR, (size_t)World.Volume);
    memset(Gen_Blocks, BLOCK_BEDROCK, (size_t)World.OneY);

    int water = delta_waterline();
    int cx = World.Width / 2;
    int cz = World.Length / 2;
    for (int z = 0; z < World.Length; z++) {
        for (int x = 0; x < World.Width; x++) {
            int height = delta_height(x, z);
            int index = World_Pack(x, 1, z);
            for (int y = 1; y <= height; y++, index += World.OneY) {
                if (y == height) {
                    Gen_Blocks[index] = surface_block(x, y, z);
                } else if (y < height - 2) {
                    Gen_Blocks[index] = BLOCK_STONE;
                } else {
                    Gen_Blocks[index] = BLOCK_DIRT;
                }
            }
            for (int y = height + 1; y <= water; y++, index += World.OneY) {
                Gen_Blocks[index] = BLOCK_STILL_WATER;
            }
        }
        Gen_CurrentProgress = (float)(z + 1) / (float)World.Length;
    }

    Gen_CurrentProgress = 0.0f;
    Gen_CurrentState = "Settle the drift groves";
    for (int z = 1; z < World.MaxZ; z++) {
        for (int x = 1; x < World.MaxX; x++) {
            if (distance(x, cx) <= 6 && distance(z, cz) <= 6) continue;
            uint32_t shape = sample(x, z, 9);
            if (shape % 113u != 0) continue;
            place_driftwood(x, z, delta_height(x, z), shape);
        }
        Gen_CurrentProgress = (float)(z + 1) / (float)World.Length;
    }

    /* Seed the nearby ridge without placing any blocks in the spawn lane. */
    static const int offsets[][2] = { { 4, 3 }, { -4, 2 }, { 4, -4 } };
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        int x = cx + offsets[i][0], z = cz + offsets[i][1];
        place_driftwood(x, z, delta_height(x, z), i);
    }
    Gen_CurrentProgress = 1.0f;
    Gen_MarkComplete();
}

const struct MapGenerator SiltDeltaGen = { delta_prepare, delta_generate };
