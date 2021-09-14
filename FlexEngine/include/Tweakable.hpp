#pragma once

#include "Variant.hpp"
#include "Platform/Platform.hpp" // For DirectoryWatcher

namespace flex
{
	// Inspired by le_tweakable from https://github.com/tgfrerer/island (which in turn is inspired by https://blog.tuxedolabs.com/2018/03/13/hot-reloading-hardcoded-parameters.html)
	struct Tweakable
	{
		u32 lineNumber;
		Variant data;
		Tweakable* next = nullptr;

#define TWEAKABLE_CTOR(type) \
	Tweakable(u32 lineNumber, type field_val) : \
		lineNumber(lineNumber), data(field_val) \
	{}

		TWEAKABLE_CTOR(i32);
		TWEAKABLE_CTOR(u32);
		TWEAKABLE_CTOR(i64);
		TWEAKABLE_CTOR(u64);
		TWEAKABLE_CTOR(real);
		TWEAKABLE_CTOR(bool);
		TWEAKABLE_CTOR(char);

#undef TWEAKABLE_CTOR
	};

	class FileWatcher
	{
	public:
		FileWatcher(const char* filePath, void* userData);

		bool Update();

		const char* filePath;
		DirectoryWatcher directoryWatcher;
	};

	i32 AddTweakableWatch(Tweakable* tweakable, const char* filePath);
	void UpdateTweakables();
	void DestroyTweakables();

#if DEBUG
#define TWEAKABLE(val)														\
	[](auto value, u32 lineNumber, const char* filePath) {					\
		static Tweakable tweakable(lineNumber, value);						\
		static i32 valueID = AddTweakableWatch(&tweakable, filePath);		\
		FLEX_UNUSED(valueID);												\
		return reinterpret_cast<decltype(val) &>(tweakable.data.valInt);	\
	}(val, __LINE__, __FILE__)

#define UPDATE_TWEAKABLES() \
	UpdateTweakables();
#define DESTROY_TWEAKABLES() \
	DestroyTweakables();
#else
	// Dissolve away tweakable interface in release builds
#define TWEAKABLE(val) val
#define UPDATE_TWEAKABLES()
#define DESTROY_TWEAKABLES()
#endif
}// namespace flex
