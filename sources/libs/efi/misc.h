#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int u16strcmp(const uint16_t *l, const uint16_t *r);
size_t u16strlen(const uint16_t *str);
uint16_t *to_u16strncpy(uint16_t *dst, const char *src, size_t size);