
#include "stdafx.hpp"

#include "Functors.hpp"

namespace flex
{
	bool strCmp::operator()(const char* a, const char* b) const
	{
		return strcmp(a, b) < 0;
	}
} // namespace flex
