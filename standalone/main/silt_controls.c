// SPDX-License-Identifier: GPL-3.0-only
#include "silt_controls.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "bsp_lcd.h"
#include "bsp_pins.h"
#include "bsp_touch.h"
#include "Event.h"
#include "Input.h"
#include "World.h"
#include "holo_port.h"
#include "Audio.h"
#include "silt_font.h"
#include "silt_rules.h"

#include "font8x8_basic.h"

typedef struct { int x, y, w, h; } Box;
typedef enum { Z_NONE, Z_WALK, Z_LOOK, Z_WORK, Z_SET, Z_PACK, Z_NEXT, Z_SAVE, Z_MUSIC, Z_SOUNDS } Zone;
typedef struct { uint16_t *buf; int y, rows; } Band;

static const uint16_t dark = 0x10c7;
static const uint16_t slate = 0x214b;
static const uint16_t chalk = 0xe75a;
static const uint16_t tide = 0x6d53;
static const uint16_t gold = 0xe629;
static const uint16_t red = 0xdb6b;
static const Box stride = {8, 171, 85, 60};
static const Box work = {250, 177, 62, 23};
static const Box set = {250, 207, 62, 23};
static const Box pack = {99, 207, 46, 23};
static const Box next = {151, 207, 43, 23};
static const Box save = {200, 207, 44, 23};
static const Box music = {245, 6, 67, 21};
static const Box effects = {245, 31, 67, 21};

static atomic_bool s_show_controls;
static atomic_bool s_show_loading = true;
static atomic_bool s_show_pack;
static atomic_bool s_music_enabled;
static atomic_bool s_effects_enabled;
static Zone s_zone;
static int s_last_x, s_last_y;
static int s_press_x, s_press_y;
static bool s_down;

static bool contains(Box b, int x, int y)
{
    return (unsigned)(x - b.x) < (unsigned)b.w &&
           (unsigned)(y - b.y) < (unsigned)b.h;
}

static void rect(Band b, int x, int y, int w, int h, uint16_t color)
{
    int bottom = y + h < b.y + b.rows ? y + h : b.y + b.rows;
    if (y < b.y) y = b.y;
    for (; y < bottom; y++) {
        uint16_t *row = b.buf + (y - b.y) * 320 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}


static void label(Band b, int x, int y, const char *message, uint16_t color, int scale)
{
    silt_text_draw(b.buf, b.y, b.rows, x, y, message, color, scale);
}

static void button(Band b, Box box, const char *name, uint16_t color)
{
    rect(b, box.x, box.y, box.w, box.h, dark);
    rect(b, box.x, box.y, 3, box.h, color);
    rect(b, box.x + 3, box.y, box.w - 3, 1, slate);
    int width = silt_text_width(name, 1);
    label(b, box.x + (box.w - width + 2) / 2, box.y + (box.h - 14) / 2,
          name, color, 1);
}

static Zone zone_at(int x, int y)
{
    if (contains(stride, x, y)) return Z_WALK;
    if (contains(work, x, y)) return Z_WORK;
    if (contains(set, x, y)) return Z_SET;
    if (contains(pack, x, y)) return Z_PACK;
    if (contains(next, x, y)) return Z_NEXT;
    if (contains(save, x, y)) return Z_SAVE;
    if (contains(music, x, y)) return Z_MUSIC;
    if (contains(effects, x, y)) return Z_SOUNDS;
    return Z_LOOK;
}

static int axis(int value)
{
    if (value < -30) value = -30;
    if (value > 30) value = 30;
    if (abs(value) < 4) return 0;
    return value * 1092;
}

static void pack_press(int x, int y)
{
    if (contains((Box){242, 31, 28, 20}, x, y)) {
        atomic_store(&s_show_pack, false);
        return;
    }
    for (unsigned i = 0; i < SV_RECIPE_COUNT; i++) {
        if (contains((Box){55, 86 + 24 * (int)i, 210, 20}, x, y)) {
            silt_rules_craft(i);
            return;
        }
    }
    if (contains((Box){55, 187, 210, 23}, x, y)) silt_rules_eat();
}

void silt_controls_init(void)
{
    gpio_config_t boot = {
        .pin_bit_mask = 1ull << BSP_BTN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&boot);
    silt_rules_init();
}

bool silt_controls_active(void)
{
    return atomic_load(&s_show_controls);
}

void silt_controls_poll(float delta)
{
    bsp_touch_raw_t sample;
    int x = s_last_x, y = s_last_y;
    bool down = bsp_touch_poll(&sample);
    if (down) bsp_touch_map(&sample, &x, &y);

    bool playing = World.Blocks && Input.RawMode;
    atomic_store(&s_show_controls, playing);
    atomic_store(&s_show_loading, !World.Blocks);
    if (!playing) {
        if (down) Pointer_SetPosition(0, x, y);
        Input_Set(CCMOUSE_L, down);
        holo_port_reset_input();
        atomic_store(&s_show_pack, false);
        s_down = false;
        s_zone = Z_NONE;
        return;
    }
    Input_SetReleased(CCMOUSE_L);

    if (atomic_load(&s_show_pack)) {
        if (down && !s_down) { s_press_x = x; s_press_y = y; }
        if (!down && s_down && abs(x - s_press_x) + abs(y - s_press_y) < 24) {
            pack_press(x, y);
        }
        holo_port_reset_input();
        s_down = down;
        s_last_x = x;
        s_last_y = y;
        return;
    }

    if (down && !s_down) s_zone = zone_at(x, y);
    if (down && s_down && s_zone == Z_LOOK) {
        Event_RaiseRawMove(&PointerEvents.RawMoved, (float)(x - s_last_x), (float)(y - s_last_y));
    }
    if (!down && s_down && s_zone == zone_at(x, y)) {
        if (s_zone == Z_PACK) atomic_store(&s_show_pack, true);
        if (s_zone == Z_NEXT) silt_rules_select_next();
        if (s_zone == Z_SAVE) silt_rules_save();
        if (s_zone == Z_MUSIC) {
            bool playing_music = !atomic_load(&s_music_enabled);
            Audio_SetMusic(playing_music ? 35 : 0);
            atomic_store(&s_music_enabled, Audio_MusicVolume != 0);
        }
        if (s_zone == Z_SOUNDS) {
            bool playing_effects = !atomic_load(&s_effects_enabled);
            Audio_SetSounds(playing_effects ? 100 : 0);
            atomic_store(&s_effects_enabled, Audio_SoundsVolume != 0);
        }
    }

    int lx = down && s_zone == Z_WALK ? axis(x - 50) : 0;
    int ly = down && s_zone == Z_WALK ? axis(203 - y) : 0;
    bool chipping = down && s_zone == Z_WORK;
    uint32_t buttons = gpio_get_level(BSP_BTN_BOOT) == 0 ? 16u : 0u;
    if (chipping) buttons |= 256u;
    if (down && s_zone == Z_SET) buttons |= 512u;
    holo_port_set_input(buttons, lx, ly, 0, 0);
    silt_rules_tick(delta, lx || ly, chipping);
    s_down = down;
    s_last_x = x;
    s_last_y = y;
    if (!down) s_zone = Z_NONE;
}

static void meter(Band b, int x, int y, unsigned value, int total, uint16_t c)
{
    rect(b, x, y, 44, 5, slate);
    if (value > (unsigned)total) value = total;
    rect(b, x, y, (int)(44 * value / total), 5, c);
}

static void pack_draw(Band b, const SiltRulesSnapshot *s)
{
    static const char *const recipes[SV_RECIPE_COUNT] = {
        "木楔 2浮木+1芦苇", "页岩锤 3页岩+1浮木",
        "苇格栅 3芦苇+1页岩", "树脂灯 1树脂+2页岩"
    };
    rect(b, 44, 23, 232, 194, dark);
    rect(b, 44, 23, 232, 2, tide);
    label(b, 56, 35, "工具包", chalk, 2);
    button(b, (Box){242,31,28,20}, "关", gold);
    char line[62];
    if (b.y < 80 && b.y + b.rows > 64) {
        snprintf(line, sizeof(line), "浮木 %u  苇 %u  石 %u", s->items[SV_DRIFTWOOD],
                 s->items[SV_REED], s->items[SV_SHALE]);
        label(b, 56, 67, line, chalk, 1);
    }
    for (unsigned i = 0; i < SV_RECIPE_COUNT; i++) {
        int y = 86 + 24 * (int)i;
        rect(b, 55, y, 210, 20, slate);
        rect(b, 55, y + 2, 2, 16, tide);
        label(b, 62, y + 3, recipes[i], chalk, 1);
    }
    if (b.y < 211 && b.y + b.rows > 187) {
        snprintf(line, sizeof(line), "食用种荚 %u", s->items[SV_SEED_POD]);
        button(b, (Box){55,187,210,23}, line, gold);
    }
}

void silt_draw_band(uint16_t *dst, int y0, int rows)
{
    Band b = {dst, y0, rows};
    if (!silt_controls_active()) {
        if (atomic_load(&s_show_loading)) {
            rect(b, 0, y0, 320, rows, dark);
            rect(b, 48, 80, 224, 2, tide);
            label(b, 96, 98, "泥岸行旅", chalk, 2);
            label(b, 112, 143, "正在进入河口", gold, 1);
        }
        return;
    }
    SiltRulesSnapshot state;
    silt_rules_snapshot(&state);

    rect(b, 7, 6, 120, 54, dark);
    label(b, 14, 9, "泥岸行旅", chalk, 1);
    label(b, 14, 25, "生命", chalk, 1);
    label(b, 14, 42, "体力", chalk, 1);
    meter(b, 63, 30, state.health, SV_MAX_HEALTH, red);
    meter(b, 63, 47, state.energy, SV_MAX_ENERGY, gold);
    if (state.night) button(b, (Box){181,6,56,19}, "夜间", gold);
    button(b, music, atomic_load(&s_music_enabled) ? "音乐开" : "音乐关", tide);
    button(b, effects, atomic_load(&s_effects_enabled) ? "音效开" : "音效关", tide);
    rect(b, 156, 119, 2, 2, chalk);
    rect(b, 162, 119, 2, 2, chalk);
    rect(b, 159, 116, 2, 2, tide);
    rect(b, 159, 122, 2, 2, tide);

    rect(b, stride.x, stride.y, stride.w, stride.h, dark);
    rect(b, 50, 177, 1, 48, tide);
    rect(b, 18, 201, 66, 1, tide);
    rect(b, 43, 196, 15, 11, slate);
    label(b, 46, 180, "^", tide, 1);
    label(b, 23, 197, "<", tide, 1);
    label(b, 70, 197, ">", tide, 1);
    label(b, 46, 212, "V", tide, 1);

    button(b, work, "采集", tide);
    button(b, set, "放置", gold);
    button(b, pack, "工具", chalk);
    button(b, next, "选材", chalk);
    button(b, save, "存档", chalk);
    if (state.gather_percent) {
        rect(b, 250, 202, 62, 2, slate);
        rect(b, 250, 202, 62 * state.gather_percent / 100, 2, tide);
    }
    if (y0 < 207 && y0 + rows > 188) {
        char slot[40];
        const char *name = SV_ResourceName(state.selected);
        unsigned count = (unsigned)state.selected < SV_RESOURCE_COUNT ? state.items[state.selected] : 0;
        snprintf(slot, sizeof(slot), "%s %u", *name ? name : "空", count);
        rect(b, 99, 190, 145, 14, dark);
        label(b, 105, 190, slot, chalk, 1);
    }
    if (atomic_load(&s_show_pack)) pack_draw(b, &state);
    if (state.notice[0]) {
        rect(b, 7, 63, 304, 18, dark);
        label(b, 14, 65, state.notice, gold, 1);
    }
}

void silt_boot_message(const char *message)
{
    enum { ROWS = 10 };
    uint16_t *pixels = heap_caps_malloc(320 * ROWS * sizeof(*pixels), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!pixels) return;
    for (int y = 70; y < 169; y += ROWS) {
        Band b = {pixels, y, ROWS};
        rect(b, 0, y, 320, ROWS, dark);
        rect(b, 25, 84, 270, 2, tide);
        label(b, 96, 104, "泥岸行旅", chalk, 2);
        label(b, (320 - silt_text_width(message, 1)) / 2, 145, message, gold, 1);
        if (bsp_lcd_blit(0, y, 320, y + ROWS, pixels) != ESP_OK) break;
    }
    free(pixels);
}
