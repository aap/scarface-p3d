#include "core.h"

namespace core
{

u32
MakeKey(const char *str)
{
	u32 key = 0;
	for(; *str; str++) {
		key *= 65599;
		key ^= (u32)*str;
	}
	return key;
}

u32
MakeKeyCI(const char *str)
{
	u32 key = 0;
	for(; *str; str++) {
		key *= 65599;
		key ^= (u32)(*str < 'a' ? *str-'A'+'a' : *str);
	}
	return key;
}

}
