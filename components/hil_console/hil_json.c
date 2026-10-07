#include "hil_json.h"

#include <inttypes.h>
#include <stdio.h>

static void put_char(hil_json_t *json, char c)
{
    if (json->overflow || json->used + 1 >= json->size) {
        json->overflow = true;
        return;
    }
    json->out[json->used++] = c;
    json->out[json->used] = '\0';
}

static void put_text(hil_json_t *json, const char *text)
{
    while (*text != '\0') {
        put_char(json, *text++);
    }
}

static void put_escaped(hil_json_t *json, const char *text)
{
    put_char(json, '"');
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0'; ++c) {
        switch (*c) {
        case '"': put_text(json, "\\\""); break;
        case '\\': put_text(json, "\\\\"); break;
        case '\n': put_text(json, "\\n"); break;
        case '\r': put_text(json, "\\r"); break;
        case '\t': put_text(json, "\\t"); break;
        default:
            if (*c < 0x20) {
                char escape[7];
                snprintf(escape, sizeof(escape), "\\u%04x", *c);
                put_text(json, escape);
            } else {
                put_char(json, (char)*c);
            }
        }
    }
    put_char(json, '"');
}

/* Separator and key before a value at the current nesting level. */
static void begin_value(hil_json_t *json, const char *key)
{
    if (json->needs_comma[json->depth]) {
        put_char(json, ',');
    }
    json->needs_comma[json->depth] = true;
    if (key != NULL) {
        put_escaped(json, key);
        put_char(json, ':');
    }
}

static void open_container(hil_json_t *json, const char *key, char opener, char closer)
{
    begin_value(json, key);
    if (json->depth >= HIL_JSON_MAX_DEPTH) {
        json->overflow = true;
        return;
    }
    put_char(json, opener);
    ++json->depth;
    json->needs_comma[json->depth] = false;
    json->closer[json->depth] = closer;
}

void hil_json_init(hil_json_t *json, char *out, size_t size)
{
    *json = (hil_json_t){.out = out, .size = size, .overflow = size == 0};
    if (size > 0) {
        out[0] = '\0';
    }
}

void hil_json_object(hil_json_t *json, const char *key)
{
    open_container(json, key, '{', '}');
}

void hil_json_array(hil_json_t *json, const char *key)
{
    open_container(json, key, '[', ']');
}

void hil_json_end(hil_json_t *json)
{
    if (json->depth == 0) {
        json->overflow = true;
        return;
    }
    put_char(json, json->closer[json->depth]);
    --json->depth;
}

void hil_json_string(hil_json_t *json, const char *key, const char *value)
{
    begin_value(json, key);
    put_escaped(json, value != NULL ? value : "");
}

void hil_json_int(hil_json_t *json, const char *key, int64_t value)
{
    char text[24];
    snprintf(text, sizeof(text), "%" PRId64, value);
    begin_value(json, key);
    put_text(json, text);
}

void hil_json_uint(hil_json_t *json, const char *key, uint64_t value)
{
    char text[24];
    snprintf(text, sizeof(text), "%" PRIu64, value);
    begin_value(json, key);
    put_text(json, text);
}

void hil_json_bool(hil_json_t *json, const char *key, bool value)
{
    begin_value(json, key);
    put_text(json, value ? "true" : "false");
}

bool hil_json_complete(const hil_json_t *json)
{
    return !json->overflow && json->depth == 0 && json->used > 0;
}
