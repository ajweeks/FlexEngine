
#ifndef IGNORE_WARNINGS_PUSH
#if defined(__clang__)
#define IGNORE_WARNINGS_PUSH \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Weverything\"")
#define IGNORE_WARNINGS_POP _Pragma("clang diagnostic pop")
#elif defined(_MSC_VER)
#define IGNORE_WARNINGS_PUSH __pragma(warning(push, 0))
#define IGNORE_WARNINGS_POP __pragma(warning(pop))
#elif defined(__GNUG__) || defined(__GNUC__)
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
#define SUPPRESS_WARN_BEGIN \
		_Pragma("GCC diagnostic push"); \
		_Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#define SUPPRESS_WARN_END  _Pragma("GCC diagnostic pop")
#elif defined(__clang__)
#define SUPPRESS_WARN_BEGIN \
		_Pragma("clang diagnostic push"); \
		_Pragma("clang diagnostic ignored \"-Wmissing-field-initializers\"")
#define SUPPRESS_WARN_END  _Pragma("clang diagnostic pop")
#else
#define SUPPRESS_WARN_BEGIN
#define SUPPRESS_WARN_END
#endif
#endif // ifndef SUPPRESS_WARN_BEGIN

#ifndef SUPPRESS_WARNING_PUSH
#define SUPPRESS_WARNING_PUSH(warningID) \
	__pragma(warning(push)) \
	__pragma(warning(disable:warningID))
#endif // #ifndef SUPPRESS_WARNING_PUSH

#ifndef SUPPRESS_WARNING_POP
#define SUPPRESS_WARNING_POP() \
	__pragma(warning(pop))
#endif // #ifndef SUPPRESS_WARNING_POP
