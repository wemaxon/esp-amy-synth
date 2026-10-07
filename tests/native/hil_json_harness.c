#include "test_check.h"
#include <stdint.h>
#include <string.h>

#include "hil_json.h"

static char out[256];

/* Objects, arrays and every value type, with commas placed by nesting level. */
int32_t host_vector_json_structure(void)
{
    hil_json_t json;
    hil_json_init(&json, out, sizeof(out));
    hil_json_object(&json, NULL);
    hil_json_string(&json, "mode", "PLAY");
    hil_json_int(&json, "octave", -1);
    hil_json_uint(&json, "frame", 4294967296ULL);
    hil_json_bool(&json, "on", true);
    hil_json_array(&json, "notes");
    hil_json_int(&json, NULL, 60);
    hil_json_int(&json, NULL, 64);
    hil_json_end(&json);
    hil_json_array(&json, "tracks");
    hil_json_object(&json, NULL);
    hil_json_string(&json, "state", "PLAY");
    hil_json_end(&json);
    hil_json_object(&json, NULL);
    hil_json_end(&json);
    hil_json_end(&json);
    hil_json_end(&json);
    return hil_json_complete(&json) &&
           strcmp(out, "{\"mode\":\"PLAY\",\"octave\":-1,\"frame\":4294967296,\"on\":true,"
                       "\"notes\":[60,64],\"tracks\":[{\"state\":\"PLAY\"},{}]}") == 0;
}

/* Strings are escaped, so any name or message stays valid JSON. */
int32_t host_vector_json_escaping(void)
{
    hil_json_t json;
    hil_json_init(&json, out, sizeof(out));
    hil_json_object(&json, NULL);
    hil_json_string(&json, "error", "bad \"x\"\\\n\x01");
    hil_json_end(&json);
    return hil_json_complete(&json) &&
           strcmp(out, "{\"error\":\"bad \\\"x\\\"\\\\\\n\\u0001\"}") == 0;
}

/* Brackets inside strings do not affect nesting. */
int32_t host_vector_json_brackets_in_strings(void)
{
    hil_json_t json;
    hil_json_init(&json, out, sizeof(out));
    hil_json_object(&json, NULL);
    hil_json_array(&json, "names");
    hil_json_string(&json, NULL, "a]}{[");
    hil_json_end(&json);
    hil_json_end(&json);
    return hil_json_complete(&json) && strcmp(out, "{\"names\":[\"a]}{[\"]}") == 0;
}

/* A reply that does not fit is reported, never emitted half-written. */
int32_t host_vector_json_overflow(void)
{
    char small[12];
    hil_json_t json;
    hil_json_init(&json, small, sizeof(small));
    hil_json_object(&json, NULL);
    hil_json_string(&json, "mode", "PLAY ONESHOT");
    hil_json_end(&json);
    if (hil_json_complete(&json)) return 0;
    /* Unclosed containers are not complete either. */
    hil_json_init(&json, out, sizeof(out));
    hil_json_object(&json, NULL);
    return !hil_json_complete(&json) && memchr(small, '\0', sizeof(small)) != NULL;
}

int main(void)
{
    CHECK_VECTOR(host_vector_json_structure, 1);
    CHECK_VECTOR(host_vector_json_escaping, 1);
    CHECK_VECTOR(host_vector_json_brackets_in_strings, 1);
    CHECK_VECTOR(host_vector_json_overflow, 1);
    return 0;
}
