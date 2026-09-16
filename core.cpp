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

// Key used for LoadInventory UIDs (Entity::GetUID, Find). Retail exe 0x6dc190:
// masks to 31 bits after every multiply, folds chars < 'a' by +0x20, sets the top bit.
u32
GetHash(const char *str, u32 seed)
{
	if(str == nil || *str == '\0')
		return seed;
	u32 key = seed & 0x7fffffff;
	for(; *str; str++) {
		key = (key*65599) & 0x7fffffff;
		i8 c = *str;
		key ^= (u32)(i32)(c < 'a' ? c+0x20 : c);
	}
	return key | 0x80000000;
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
