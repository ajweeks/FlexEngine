#pragma once

namespace flex
{
	template<class T1, class T2>
	class Pair
	{
	public:
		Pair<T1, T2>()
		{
		}

		Pair<T1, T2>(const T1& t1, const T2& t2) :
			first(t1), second(t2)
		{
		}

		Pair<T1, T2>& operator=(const Pair& other)
		{
			first = other.first;
			second = other.second;
			return *this;
		}

		Pair<T1, T2>& operator=(const Pair&& other)
		{
			first = other.first;
			second = other.second;
			return *this;
		}

		Pair<T1, T2>(const Pair<T1, T2>& other)
		{
			first = other.first;
			second = other.second;
		}

		Pair<T1, T2>(const Pair<T1, T2>&& other)
		{
			first = other.first;
			second = other.second;
		}

		T1 first;
		T2 second;
	};

} // namespace flex
