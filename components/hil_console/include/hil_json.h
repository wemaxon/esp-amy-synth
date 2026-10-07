#ifndef HIL_JSON_H
#define HIL_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal streaming JSON writer into a caller buffer. Commas are placed by
 * nesting level and strings are escaped. Pass key NULL for top-level values and
 * array elements. If the buffer is too small, writing stops and
 * hil_json_complete() reports false.
 */
#define HIL_JSON_MAX_DEPTH 8

typedef struct {
    char *out;
    size_t size;
    size_t used;
    bool overflow;
    uint8_t depth;
    bool needs_comma[HIL_JSON_MAX_DEPTH + 1];
    char closer[HIL_JSON_MAX_DEPTH + 1]; /* '}' or ']' per open container */
} hil_json_t;

void hil_json_init(hil_json_t *json, char *out, size_t size);
void hil_json_object(hil_json_t *json, const char *key);
void hil_json_array(hil_json_t *json, const char *key);
void hil_json_end(hil_json_t *json);
void hil_json_string(hil_json_t *json, const char *key, const char *value);
void hil_json_int(hil_json_t *json, const char *key, int64_t value);
void hil_json_uint(hil_json_t *json, const char *key, uint64_t value);
void hil_json_bool(hil_json_t *json, const char *key, bool value);
/* True when every container is closed and nothing was truncated. */
bool hil_json_complete(const hil_json_t *json);

#ifdef __cplusplus
}
#endif

#endif
