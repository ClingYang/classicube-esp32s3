/* SPDX-License-Identifier: GPL-3.0-only */
#include "silt_rules.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Block.h"
#include "Entity.h"
#include "Game.h"
#include "Inventory.h"
#include "Picking.h"
#include "Platform.h"
#include "World.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "holo_port.h"

enum { DROP_SLOTS = 12, SAVE_HEADER = 84, MAX_MAP_BYTES = 2 * 1024 * 1024, IO_CHUNK = 2048 };

typedef struct {
    IVec3 block;
    SV_Resource item;
    uint16_t age;
    bool active;
} Drop;

static const char *TAG = "silt_rules";
static SV_State s_rules;
static SV_Resource s_selected;
static Drop s_drops[DROP_SLOTS];
static unsigned s_drop_cursor;
static char s_notice[sizeof(((SiltRulesSnapshot *)0)->notice)];
static unsigned s_notice_left, s_shelter_wait;
static bool s_attempted_restore, s_sheltered, s_previous_night, s_falling;
static float s_time_fraction, s_fall_top;
static uint32_t s_save_serial;
static int s_save_slot = -1;
static portMUX_TYPE s_display_lock = portMUX_INITIALIZER_UNLOCKED;
static SiltRulesSnapshot s_display;

static const SV_Resource building[] = { SV_DRIFTWOOD, SV_SHALE, SV_LATTICE, SV_LANTERN };
enum { BUILDING_TYPES = sizeof(building) / sizeof(building[0]) };

static void notice(const char *text)
{
    snprintf(s_notice, sizeof(s_notice), "%s", text);
    s_notice_left = 2400;
}

static void pickup_notice(SV_Resource item)
{
    snprintf(s_notice, sizeof(s_notice), "已拾取%s", SV_ResourceName(item));
    s_notice_left = 2400;
}

static void publish(void)
{
    SiltRulesSnapshot next = {
        .health = s_rules.health, .energy = s_rules.energy,
        .gather_percent = SV_GatherPercent(&s_rules), .light = SV_Daylight(&s_rules),
        .selected = s_selected, .night = SV_IsNight(&s_rules)
    };
    memcpy(next.items, s_rules.items, sizeof(next.items));
    memcpy(next.tools, s_rules.tools, sizeof(next.tools));
    if (s_notice_left) memcpy(next.notice, s_notice, sizeof(next.notice));
    portENTER_CRITICAL(&s_display_lock);
    s_display = next;
    portEXIT_CRITICAL(&s_display_lock);
}

void silt_rules_snapshot(SiltRulesSnapshot *out)
{
    portENTER_CRITICAL(&s_display_lock);
    *out = s_display;
    portEXIT_CRITICAL(&s_display_lock);
}

static SV_Resource from_block(BlockID block)
{
    switch (block) {
    case BLOCK_GRASS:
    case BLOCK_LEAVES:
    case BLOCK_SAPLING:
    case BLOCK_DANDELION:
    case BLOCK_ROSE: return SV_REED;
    case BLOCK_LOG:
    case BLOCK_WOOD: return SV_DRIFTWOOD;
    case BLOCK_STONE:
    case BLOCK_COBBLE:
    case BLOCK_DIRT:
    case BLOCK_SAND:
    case BLOCK_GRAVEL: return SV_SHALE;
    case BLOCK_CRATE: return SV_LATTICE;
    case BLOCK_CERAMIC_TILE: return SV_LANTERN;
    default: return SV_RESOURCE_COUNT;
    }
}

static BlockID building_block(SV_Resource item)
{
    switch (item) {
    case SV_DRIFTWOOD: return BLOCK_LOG;
    case SV_SHALE: return BLOCK_STONE;
    case SV_LATTICE: return BLOCK_CRATE;
    case SV_LANTERN: return BLOCK_CERAMIC_TILE;
    default: return BLOCK_AIR;
    }
}

static void sync_inventory(void)
{
    if (!s_rules.items[s_selected]) {
        for (unsigned i = 0; i < BUILDING_TYPES; i++) {
            if (s_rules.items[building[i]]) {
                s_selected = building[i];
                break;
            }
        }
    }
    BlockID chosen = s_rules.items[s_selected] ? building_block(s_selected) : BLOCK_AIR;
    if (Inventory_SelectedBlock != chosen) Inventory_SetSelectedBlock(chosen);
}

static bool valid_site(IVec3 pos)
{
    return World.Blocks && World_Contains(pos.x, pos.y, pos.z);
}

static uint32_t site_id(IVec3 pos)
{
    return (uint32_t)World_Pack(pos.x, pos.y, pos.z);
}

static void drop_resource(IVec3 pos, SV_Resource item)
{
    while (pos.y > 0) {
        BlockID below = World_GetBlock(pos.x, pos.y - 1, pos.z);
        if (Blocks.Collide[below] == COLLIDE_SOLID) break;
        pos.y--;
    }
    for (unsigned i = 0; i < DROP_SLOTS; i++) {
        unsigned index = (s_drop_cursor + i) % DROP_SLOTS;
        if (s_drops[index].active) continue;
        s_drops[index] = (Drop){ .block = pos, .item = item, .active = true };
        s_drop_cursor = (index + 1) % DROP_SLOTS;
        notice("资源已掉落");
        return;
    }
    notice("散落资源太多");
}

static void tick_drops(Vec3 player, unsigned elapsed)
{
    bool added = false;
    for (unsigned i = 0; i < DROP_SLOTS; i++) {
        Drop *drop = &s_drops[i];
        if (!drop->active) continue;
        drop->age += (uint16_t)elapsed;
        if (drop->age >= 18000) {
            drop->active = false;
            continue;
        }
        if (drop->age < 180) continue;
        float dx = player.x - (drop->block.x + 0.5f);
        float dz = player.z - (drop->block.z + 0.5f);
        float dy = player.y - drop->block.y;
        if (dx * dx + dz * dz > 2.56f || dy < -1.6f || dy > 1.7f) continue;
        if (!SV_Add(&s_rules, drop->item, 1)) continue;
        drop->active = false;
        added = true;
        pickup_notice(drop->item);
    }
    if (added) sync_inventory();
}

static bool sheltered(Vec3 feet)
{
    int x = (int)floorf(feet.x);
    int y = (int)floorf(feet.y);
    int z = (int)floorf(feet.z);
    if (!World_Contains(x, y, z)) return false;
    for (int next = y + 2; next < World.Height; next++) {
        BlockID roof = World_GetBlock(x, next, z);
        if (roof != BLOCK_LEAVES && Blocks.Collide[roof] == COLLIDE_SOLID) return true;
    }
    for (int dy = -1; dy <= 2; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            for (int dz = -3; dz <= 3; dz++) {
                if (dx * dx + dz * dz > 10) continue;
                if (World_SafeGetBlock(x + dx, y + dy, z + dz) == BLOCK_CERAMIC_TILE) return true;
            }
        }
    }
    return false;
}

static void set_sky(bool night)
{
    if (night) {
        Env_SetSkyCol(PackedCol_Make(38, 46, 69, 255));
        Env_SetFogCol(PackedCol_Make(57, 68, 82, 255));
        Env_SetCloudsCol(PackedCol_Make(90, 96, 106, 255));
        Env_SetSunCol(PackedCol_Make(123, 131, 151, 255));
        Env_SetShadowCol(PackedCol_Make(73, 80, 95, 255));
    } else {
        Env_SetSkyCol(PackedCol_Make(143, 184, 202, 255));
        Env_SetFogCol(PackedCol_Make(174, 196, 208, 255));
        Env_SetCloudsCol(PackedCol_Make(228, 234, 239, 255));
        Env_SetSunCol(PackedCol_Make(247, 247, 247, 255));
        Env_SetShadowCol(PackedCol_Make(150, 166, 176, 255));
    }
    s_previous_night = night;
}

static void tick_fall(const struct Entity *player)
{
    float height = player->Position.y;
    if (!player->OnGround) {
        if (!s_falling || height > s_fall_top) s_fall_top = height;
        s_falling = true;
        return;
    }
    if (s_falling && height < s_fall_top) {
        unsigned health = s_rules.health;
        SV_Land(&s_rules, sqrtf(46.0f * (s_fall_top - height)));
        if (s_rules.health < health) notice("落地受伤");
    }
    s_falling = false;
}

static void respawn(struct LocalPlayer *player)
{
    struct LocationUpdate new_pos = { 0 };
    LocalPlayer_CalcDefaultSpawn(player, &new_pos);
    LocalPlayers_MoveToSpawn(&new_pos);
    player->Base.Velocity = (Vec3){0, 0, 0};
    SV_Respawn(&s_rules);
    s_falling = false;
    sync_inventory();
    notice("已在河岸重生");
}

static uint32_t load_u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void store_u32(uint8_t *b, uint32_t data)
{
    for (unsigned i = 0; i < 4; i++) b[i] = (uint8_t)(data >> (8 * i));
}

_Static_assert(sizeof(float) == 4 && sizeof(BlockRaw) == 1,
               "The world save expects 32-bit floats and 8-bit map blocks");

static float load_float(const uint8_t *b)
{
    uint32_t bits = load_u32(b);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void store_float(uint8_t *b, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    store_u32(b, bits);
}

static uint32_t checksum(const uint8_t *data, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++) seed = (seed ^ data[i]) * 16777619u;
    return seed;
}

static bool within_world(Vec3 p, unsigned width, unsigned height, unsigned length)
{
    return p.x >= 0 && p.x < (float)width && p.y >= 0 && p.y < (float)height &&
           p.z >= 0 && p.z < (float)length;
}

static bool slot_path(char *path, size_t size, int slot, bool temporary)
{
    int length = snprintf(path, size, "%s/maps/silt%d.svw%s", holo_port_root(), slot,
                          temporary ? ".tmp" : "");
    return length > 0 && (size_t)length < size;
}

static size_t map_volume(unsigned w, unsigned h, unsigned l)
{
    if (!w || w > 512 || !h || h > 256 || !l || l > 512) return 0;
    size_t volume = (size_t)w * h * l;
    return volume <= MAX_MAP_BYTES ? volume : 0;
}

typedef struct {
    uint32_t serial, seed, map_hash;
    unsigned width, height, length;
    size_t volume;
    Vec3 position;
    float yaw, pitch;
    SV_State rules;
} SavedWorld;

static bool read_header(FILE *file, SavedWorld *out)
{
    uint8_t h[SAVE_HEADER];
    if (fread(h, 1, sizeof(h), file) != sizeof(h) || memcmp(h, "SLT1", 4) ||
        load_u32(h + 80) != checksum(h, 80, 2166136261u)) return false;
    out->serial = load_u32(h + 4);
    out->width = load_u32(h + 8);
    out->height = load_u32(h + 12);
    out->length = load_u32(h + 16);
    out->seed = load_u32(h + 20);
    out->position = (Vec3){load_float(h + 24), load_float(h + 28), load_float(h + 32)};
    out->yaw = load_float(h + 36);
    out->pitch = load_float(h + 40);
    out->volume = map_volume(out->width, out->height, out->length);
    out->map_hash = load_u32(h + 76);
    return out->volume && within_world(out->position, out->width, out->height, out->length) &&
           out->yaw >= -720 && out->yaw <= 720 && out->pitch >= -720 && out->pitch <= 720 &&
           SV_Load(&out->rules, h + 44, SV_SAVE_SIZE);
}

static bool read_slot_header(int slot, SavedWorld *out)
{
    char path[160];
    if (!slot_path(path, sizeof(path), slot, false)) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool valid = read_header(file, out);
    fclose(file);
    return valid;
}

static BlockRaw *read_world(FILE *file, const SavedWorld *saved)
{
    BlockRaw *blocks = Mem_TryAlloc(saved->volume, 1);
    if (!blocks) return NULL;
    uint8_t *scratch = heap_caps_malloc(IO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!scratch) { Mem_Free(blocks); return NULL; }
    size_t at = 0;
    uint32_t sum = 2166136261u;
    while (at < saved->volume) {
        size_t chunk = saved->volume - at;
        if (chunk > IO_CHUNK) chunk = IO_CHUNK;
        if (fread(scratch, 1, chunk, file) != chunk) break;
#if defined(CC_BUILD_TINYMEM) && !defined(EXTENDED_BLOCKS)
        bool allowed = true;
        for (size_t i = 0; i < chunk; i++) allowed = allowed && scratch[i] < BLOCK_COUNT;
        if (!allowed) break;
#endif
        memcpy(blocks + at, scratch, chunk);
        sum = checksum(scratch, chunk, sum);
        at += chunk;
    }
    free(scratch);
    if (at == saved->volume && sum == saved->map_hash && fgetc(file) == EOF && !ferror(file)) return blocks;
    Mem_Free(blocks);
    return NULL;
}

static bool restore_slot(int slot)
{
    char path[160];
    if (!slot_path(path, sizeof(path), slot, false)) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    SavedWorld saved;
    BlockRaw *blocks = read_header(file, &saved) ? read_world(file, &saved) : NULL;
    fclose(file);
    if (!blocks) return false;

    Game_Reset();
    World.Seed = (int32_t)saved.seed;
    World_SetNewMap(blocks, saved.width, saved.height, saved.length);
    struct LocalPlayer *player = Entities.CurPlayer;
    struct LocationUpdate spawn = { 0 };
    LocalPlayer_CalcDefaultSpawn(player, &spawn);
    LocalPlayers_MoveToSpawn(&spawn);
    struct LocationUpdate location = {
        .pos = saved.position, .yaw = saved.yaw, .pitch = saved.pitch,
        .flags = LU_HAS_POS | LU_HAS_YAW | LU_HAS_PITCH
    };
    player->Base.VTABLE->SetLocation(&player->Base, &location);
    player->Base.Velocity = (Vec3){0, 0, 0};
    s_rules = saved.rules;
    s_save_slot = slot;
    s_save_serial = saved.serial;
    notice("已恢复生存存档");
    ESP_LOGI(TAG, "restored slot %d, save %lu", slot, (unsigned long)saved.serial);
    return true;
}

static void try_restore(void)
{
    SavedWorld a, b;
    bool first = read_slot_header(0, &a);
    bool second = read_slot_header(1, &b);
    uint32_t delta = first && second ? b.serial - a.serial : 0;
    if (second && (!first || (delta && delta < UINT32_C(0x80000000)))) {
        if (!restore_slot(1) && first) restore_slot(0);
    } else if (first && !restore_slot(0) && second) {
        restore_slot(1);
    }
}

static bool write_slot(int slot, uint32_t serial, const struct Entity *player)
{
    char path[160], temp[160];
    if (!slot_path(path, sizeof(path), slot, false) ||
        !slot_path(temp, sizeof(temp), slot, true)) return false;

    size_t volume = map_volume(World.Width, World.Height, World.Length);
    if (!volume || volume != (size_t)World.Volume ||
        !within_world(player->Position, World.Width, World.Height, World.Length)) return false;
#ifdef EXTENDED_BLOCKS
    if (World.Blocks2 != World.Blocks) return false;
#endif

    uint8_t h[SAVE_HEADER] = { 'S', 'L', 'T', '1' };
    store_u32(h + 4, serial);
    store_u32(h + 8, (uint32_t)World.Width);
    store_u32(h + 12, (uint32_t)World.Height);
    store_u32(h + 16, (uint32_t)World.Length);
    store_u32(h + 20, (uint32_t)World.Seed);
    store_float(h + 24, player->Position.x);
    store_float(h + 28, player->Position.y);
    store_float(h + 32, player->Position.z);
    store_float(h + 36, fmodf(player->Yaw, 360.0f));
    store_float(h + 40, fmodf(player->Pitch, 360.0f));
    if (!SV_Save(&s_rules, h + 44, SV_SAVE_SIZE)) return false;
    store_u32(h + 76, checksum(World.Blocks, volume, 2166136261u));
    store_u32(h + 80, checksum(h, 80, 2166136261u));

    uint8_t *scratch = heap_caps_malloc(IO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!scratch) return false;
    FILE *file = fopen(temp, "wb");
    if (!file) { free(scratch); return false; }
    bool success = fwrite(h, 1, sizeof(h), file) == sizeof(h);
    for (size_t at = 0; success && at < volume;) {
        size_t chunk = volume - at;
        if (chunk > IO_CHUNK) chunk = IO_CHUNK;
        memcpy(scratch, World.Blocks + at, chunk);
        success = fwrite(scratch, 1, chunk, file) == chunk;
        at += chunk;
    }
    if (success) success = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) success = false;
    free(scratch);
    if (success && (unlink(path) == 0 || errno == ENOENT) && rename(temp, path) == 0) return true;
    unlink(temp);
    return false;
}

void silt_rules_save(void)
{
    if (!World.Loaded || !World.Blocks || !Entities.CurPlayer || !s_rules.health) {
        notice("当前没有可保存的世界");
    } else {
        int slot = s_save_slot == 0 ? 1 : 0;
        uint32_t serial = s_save_serial + 1;
        if (write_slot(slot, serial, &Entities.CurPlayer->Base)) {
            s_save_slot = slot;
            s_save_serial = serial;
            notice("已保存到存储卡");
            ESP_LOGI(TAG, "saved slot %d, save %lu", slot, (unsigned long)serial);
        } else {
            notice("存档失败");
            ESP_LOGW(TAG, "world save did not complete");
        }
    }
    publish();
}

void silt_rules_init(void)
{
    SV_Init(&s_rules);
    memset(s_drops, 0, sizeof(s_drops));
    s_drop_cursor = 0;
    s_selected = SV_DRIFTWOOD;
    s_shelter_wait = 0;
    s_notice_left = 0;
    s_time_fraction = 0;
    s_save_serial = 0;
    s_save_slot = -1;
    s_attempted_restore = false;
    s_sheltered = s_previous_night = s_falling = false;
    publish();
}

void silt_rules_tick(float seconds, bool moving, bool chipping)
{
    if (!World.Loaded || !World.Blocks || !Entities.CurPlayer) return;
    if (!s_attempted_restore) {
        s_attempted_restore = true;
        try_restore();
        sync_inventory();
        set_sky(SV_IsNight(&s_rules));
        if (!s_notice_left) notice("寻找芦苇和浮木");
    }

    /* Keep fractional milliseconds across render frames. */
    if (!(seconds > 0)) seconds = 0;
    if (seconds > 0.25f) seconds = 0.25f;
    float ms = s_time_fraction + seconds * 1000.0f;
    unsigned elapsed = (unsigned)ms;
    s_time_fraction = ms - elapsed;
    s_notice_left = elapsed < s_notice_left ? s_notice_left - elapsed : 0;

    IVec3 aim = Game_SelectedPos.pos;
    if (chipping && Game_SelectedPos.valid && valid_site(aim)) {
        SV_Resource material = from_block(World_GetBlock(aim.x, aim.y, aim.z));
        if (SV_GatherHold(&s_rules, site_id(aim), material, elapsed) == SV_GATHER_NEEDS_TOOL) {
            notice("采集页岩需先制作木楔");
        }
    } else {
        SV_GatherCancel(&s_rules);
    }

    struct LocalPlayer *player = Entities.CurPlayer;
    Vec3 feet = player->Base.Position;
    if (s_shelter_wait <= elapsed && SV_IsNight(&s_rules)) {
        s_sheltered = sheltered(feet);
        s_shelter_wait = 250;
    } else {
        s_shelter_wait = elapsed < s_shelter_wait ? s_shelter_wait - elapsed : 0;
    }
    SV_Tick(&s_rules, elapsed, moving, s_sheltered);
    bool night = SV_IsNight(&s_rules);
    if (night != s_previous_night) {
        set_sky(night);
        s_shelter_wait = 0;
        notice(night ? "入夜了，寻找庇护" : "天亮了，气温回升");
        ESP_LOGI(TAG, "%s", night ? "nightfall" : "dawn");
    }

    tick_fall(&player->Base);
    if (!s_rules.health || feet.y < -2.0f) {
        respawn(player);
        ESP_LOGI(TAG, "respawn");
    } else {
        tick_drops(feet, elapsed);
    }
    publish();
}

bool silt_rules_can_chip(IVec3 pos, BlockID block)
{
    return valid_site(pos) &&
           SV_GatherHold(&s_rules, site_id(pos), from_block(block), 0) == SV_GATHER_READY;
}

void silt_rules_chip_done(IVec3 pos, BlockID old)
{
    if (!valid_site(pos)) return;
    uint32_t site = site_id(pos);
    SV_Resource material = from_block(old);
    if (SV_GatherCommit(&s_rules, site, material)) {
        drop_resource(pos, SV_ForageDrop(material, site, (uint32_t)World.Seed));
        s_shelter_wait = 0;
        publish();
    }
}

bool silt_rules_consume_place(BlockID block)
{
    SV_Resource item = from_block(block);
    bool allowed = item < SV_RESOURCE_COUNT && building_block(item) == block &&
                   SV_Take(&s_rules, item, 1);
    if (allowed) {
        sync_inventory();
        s_shelter_wait = 0;
    } else {
        notice("缺少建造材料");
    }
    publish();
    return allowed;
}

void silt_rules_craft(unsigned index)
{
    if (index >= SV_RECIPE_COUNT || !SV_Craft(&s_rules, (SV_Recipe)index)) {
        notice("配方暂不可用");
    } else {
        sync_inventory();
        notice(SV_RecipeName((SV_Recipe)index));
    }
    publish();
}

void silt_rules_eat(void)
{
    notice(SV_EatSeed(&s_rules) ? "已吃下种荚" : "现在不能进食");
    publish();
}

void silt_rules_select_next(void)
{
    unsigned selected = 0;
    for (unsigned i = 0; i < BUILDING_TYPES; i++) {
        if (s_selected == building[i]) selected = i;
    }
    for (unsigned i = 1; i <= BUILDING_TYPES; i++) {
        SV_Resource next = building[(selected + i) % BUILDING_TYPES];
        if (!s_rules.items[next]) continue;
        s_selected = next;
        break;
    }
    sync_inventory();
    notice(s_rules.items[s_selected] ? SV_ResourceName(s_selected) : "先采集材料");
    publish();
}
