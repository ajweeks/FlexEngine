#pragma once

namespace flex
{
	struct strCmp
	{
		bool operator()(const char* a, const char* b) const;
	};

} // namespace flex
