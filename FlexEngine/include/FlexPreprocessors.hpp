
#ifndef IGNORE_WARNINGS_PUSH
#if defined(__clang__)
#define IGNORE_WARNINGS_PUSH \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Weverything\"")
#define IGNORE_WARNINGS_POP _Pragma("clang diagnostic pop")
#elif defined(_MSC_VER)
#define IGNORE_WARNINGS_PUSH __pragma(warning(push, 0))
#define IGNORE_WARNINGS_POP __pragma(warning(pop))
#elif defined(__GNUG__)
#define IGNORE_WARNINGS_PUSH \
		_Pragma("GCC diagnostic push") \
		_Pragma("GCC diagnostic ignored \"-Wall\"")
#define IGNORE_WARNINGS_POP _Pragma("GCC diagnostic pop")
#else
// Unhandled compiler
#error
#endif
#endif // ifndef IGNORE_WARNINGS_PUSH


#ifndef SUPPRESS_WARN_BEGIN
#if defined(__GNUG__)
#define SUPPRESS_WARN_BEGIN(warn) \
		_Pragma("GCC diagnostic push"); \
		_Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#define SUPPRESS_WARN_END()  _Pragma("GCC diagnostic pop")
#elif defined(__clang__)
#define SUPPRESS_WARN_BEGIN(warn) \
		_Pragma("clang diagnostic push"); \
		_Pragma("clang diagnostic ignored " #warn)
#define SUPPRESS_WARN_END()  _Pragma("clang diagnostic pop")
#else
#define SUPPRESS_WARN_BEGIN(warn)
#define SUPPRESS_WARN_END()
#endif
#endif // ifndef SUPPRESS_WARN_BEGIN

