#include "esp_pocket_audio_joystick.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float JOYSTICK_DIRECTION_DEADZONE = 0.35f;

esp_pocket_audio_joystick_direction_t esp_pocket_audio_joystick_direction_from_axes(float x, float y)
{
    float magnitude_squared = (x * x) + (y * y);
    if (magnitude_squared < (JOYSTICK_DIRECTION_DEADZONE * JOYSTICK_DIRECTION_DEADZONE)) {
        return ESP_POCKET_AUDIO_JOYSTICK_CENTER;
    }

    float angle = atan2f(y, x);
    int octant = (int)floorf(((angle + (float)M_PI) / ((float)M_PI / 4.0f)) + 0.5f) & 0x07;

    switch (octant) {
    case 0:
        return ESP_POCKET_AUDIO_JOYSTICK_LEFT;
    case 1:
        return ESP_POCKET_AUDIO_JOYSTICK_DOWN_LEFT;
    case 2:
        return ESP_POCKET_AUDIO_JOYSTICK_DOWN;
    case 3:
        return ESP_POCKET_AUDIO_JOYSTICK_DOWN_RIGHT;
    case 4:
        return ESP_POCKET_AUDIO_JOYSTICK_RIGHT;
    case 5:
        return ESP_POCKET_AUDIO_JOYSTICK_UP_RIGHT;
    case 6:
        return ESP_POCKET_AUDIO_JOYSTICK_UP;
    case 7:
    default:
        return ESP_POCKET_AUDIO_JOYSTICK_UP_LEFT;
    }
}

const char *esp_pocket_audio_joystick_direction_name(esp_pocket_audio_joystick_direction_t direction)
{
    switch (direction) {
    case ESP_POCKET_AUDIO_JOYSTICK_CENTER:
        return "CENTER";
    case ESP_POCKET_AUDIO_JOYSTICK_UP:
        return "UP";
    case ESP_POCKET_AUDIO_JOYSTICK_UP_RIGHT:
        return "UP_RIGHT";
    case ESP_POCKET_AUDIO_JOYSTICK_RIGHT:
        return "RIGHT";
    case ESP_POCKET_AUDIO_JOYSTICK_DOWN_RIGHT:
        return "DOWN_RIGHT";
    case ESP_POCKET_AUDIO_JOYSTICK_DOWN:
        return "DOWN";
    case ESP_POCKET_AUDIO_JOYSTICK_DOWN_LEFT:
        return "DOWN_LEFT";
    case ESP_POCKET_AUDIO_JOYSTICK_LEFT:
        return "LEFT";
    case ESP_POCKET_AUDIO_JOYSTICK_UP_LEFT:
        return "UP_LEFT";
    default:
        return "UNKNOWN";
    }
}
