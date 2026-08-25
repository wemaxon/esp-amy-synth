#ifndef HOST_STUB_STRING_H
#define HOST_STUB_STRING_H

typedef __SIZE_TYPE__ size_t;

void *memcpy(void *dest, const void *src, size_t count);
size_t strlcpy(char *dest, const char *src, size_t size);

#endif
