#pragma once

#include <types.h>

static inline size_t strlen(const char *str)
{
	const char *cur = str;
	while (*cur != '\0')
		cur++;
	return (size_t) (cur - str);
}
