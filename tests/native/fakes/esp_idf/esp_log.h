#ifndef HOST_ESP_LOG_H
#define HOST_ESP_LOG_H
#include <stdarg.h>
static inline void test_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}
#define ESP_LOGD(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#endif
