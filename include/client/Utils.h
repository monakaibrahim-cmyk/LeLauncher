#ifndef __UTILS_H__
#define __UTILS_H__

#if defined(_WIN32)
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <core/vector.h>

bool __read_client__(const char *path, CharVector *content);
bool __write_client__(const char *path, const CharVector *content);
unsigned int __virtual_address_2_raw_Offset__(const CharVector *image, unsigned int address);
int __hex_digi_value__(char character);
bool __convert_hex_string_2_byte_array__(const char *hex, CharVector *result);
bool __apply_laa__(CharVector *image);

#endif

#endif // __UTILS_H__
