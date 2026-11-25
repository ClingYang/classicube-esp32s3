// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include "module_abi.h"

/* Call after initializing the panel. The application owns BSP initialization. */
const module_host_api_v2 *silt_host_api(void);
void silt_host_set_sd_ready(bool ready);
void silt_host_set_audio_enabled(bool enabled);
