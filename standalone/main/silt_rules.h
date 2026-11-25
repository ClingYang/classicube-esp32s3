/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SILT_RULES_H
#define SILT_RULES_H

#include "survival.h"
#include "Vectors.h"

typedef struct {
    uint8_t health, energy, gather_percent, light;
    uint8_t items[SV_RESOURCE_COUNT], tools[SV_TOOL_COUNT];
    SV_Resource selected;
    bool night;
    char notice[64];
} SiltRulesSnapshot;

void silt_rules_init(void);
void silt_rules_tick(float seconds, bool moving, bool chipping);
bool silt_rules_can_chip(IVec3 pos, BlockID block);
void silt_rules_chip_done(IVec3 pos, BlockID old);
bool silt_rules_consume_place(BlockID block);
void silt_rules_craft(unsigned index);
void silt_rules_eat(void);
void silt_rules_select_next(void);
void silt_rules_save(void);
void silt_rules_snapshot(SiltRulesSnapshot *out);

#endif
