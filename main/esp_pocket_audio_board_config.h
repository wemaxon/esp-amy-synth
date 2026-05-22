#ifndef ESP_POCKET_AUDIO_BOARD_CONFIG_H
#define ESP_POCKET_AUDIO_BOARD_CONFIG_H

#include "sdkconfig.h"
#include "config/esp_pocket_audio_hw_types.h"

#if defined(CONFIG_ESP_POCKET_AUDIO_BOARD_AI_THINKER_A1S)
#include "boards/ai_thinker_a1s.h"
#elif defined(CONFIG_ESP_POCKET_AUDIO_BOARD_WT0132P4_A1)
#include "boards/wt0132p4_a1.h"
#else
#error "Unsupported ESP_POCKET_AUDIO_BOARD selection"
#endif

#include "config/esp_pocket_audio_app_config.h"

#ifndef ESP_POCKET_AUDIO_ADS1115_ENABLED
#define ESP_POCKET_AUDIO_ADS1115_ENABLED 1
#endif

#ifndef ESP_POCKET_AUDIO_SWITCH_EXPANDER_COUNT
#define ESP_POCKET_AUDIO_SWITCH_EXPANDER_COUNT 0
#endif

#ifndef ESP_POCKET_AUDIO_SWITCH_EXPANDER_TYPE
#define ESP_POCKET_AUDIO_SWITCH_EXPANDER_TYPE ESP_POCKET_AUDIO_SWITCH_EXPANDER_NONE
#endif

#ifndef ESP_POCKET_AUDIO_SWITCH_EXPANDER_0_PULLUP_MASK
#define ESP_POCKET_AUDIO_SWITCH_EXPANDER_0_PULLUP_MASK 0xFFFF
#endif

#endif
