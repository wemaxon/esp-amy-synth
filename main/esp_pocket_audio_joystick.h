#ifndef ESP_POCKET_AUDIO_JOYSTICK_H
#define ESP_POCKET_AUDIO_JOYSTICK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_POCKET_AUDIO_JOYSTICK_CENTER = 0,
    ESP_POCKET_AUDIO_JOYSTICK_UP,
    ESP_POCKET_AUDIO_JOYSTICK_UP_RIGHT,
    ESP_POCKET_AUDIO_JOYSTICK_RIGHT,
    ESP_POCKET_AUDIO_JOYSTICK_DOWN_RIGHT,
    ESP_POCKET_AUDIO_JOYSTICK_DOWN,
    ESP_POCKET_AUDIO_JOYSTICK_DOWN_LEFT,
    ESP_POCKET_AUDIO_JOYSTICK_LEFT,
    ESP_POCKET_AUDIO_JOYSTICK_UP_LEFT,
} esp_pocket_audio_joystick_direction_t;

esp_pocket_audio_joystick_direction_t esp_pocket_audio_joystick_direction_from_axes(float x, float y);
const char *esp_pocket_audio_joystick_direction_name(esp_pocket_audio_joystick_direction_t direction);

#ifdef __cplusplus
}
#endif

#endif
