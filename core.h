#pragma once
#include <stdint.h>

#define nil nullptr

#define nelem(array) (sizeof(array)/sizeof(array[0]))

namespace core
{

typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint8_t u8;
typedef int8_t i8;

u32 MakeKey(const char *str);
u32 MakeKeyCI(const char *str);

}
