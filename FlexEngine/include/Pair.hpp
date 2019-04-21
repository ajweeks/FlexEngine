#pragma once

namespace flex
{
	template<class T1, class T2>
	class Pair
	{
	public:
		Pair<T1, T2>(T1 t1, T2 t2) :
			first(t1), second(t2)
		{
		}

		T1 first;
		T2 second;
	};

} // namespace flex
