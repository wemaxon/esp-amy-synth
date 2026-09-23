#include "esp_timer.h"

/* Meter numerical tests use deterministic time; deadline profiling is a
 * hardware test and is not inferred from this clock. */
int64_t esp_timer_get_time(void) { return 0; }
