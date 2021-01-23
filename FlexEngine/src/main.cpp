#include "stdafx.hpp"

#include "FlexEngine.hpp"
#include "Platform/Platform.hpp"
#include "Test.hpp"

// Memory leak checking includes
#if defined(DEBUG) && defined(_WINDOWS)
#define _CRTDBG_MAP_ALLOC
#include <heapapi.h>
#endif

bool g_bShowConsole = true;

int main(int argc, char *argv[])
{
	FLEX_UNUSED(argc);
	FLEX_UNUSED(argv);

#ifdef _WINDOWS
	// Enable run-time memory leak check for debug builds
#ifdef DEBUG
	// Notify user if heap is corrupt
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

	// TODO: Somehow redirect output to console
	_CrtSetDbgFlag(
		_CRTDBG_ALLOC_MEM_DF // Turn on debug allocation
		| _CRTDBG_LEAK_CHECK_DF // Leak check at program exit
	//| _CRTDBG_CHECK_ALWAYS_DF // Check heap every alloc/dealloc
	//| _CRTDBG_CHECK_EVERY_16_DF // Check heap every 16 heap ops
	);
	//_CrtSetBreakAlloc(47947);
#endif // DEBUG
#endif // _WINDOWS

	flex::Platform::GetConsoleHandle();
	flex::InitializeLogger();

#if RUN_UNIT_TESTS
	flex::FlexTest::Run();

	system("pause");
	return 0;

#else

	{
		flex::FlexEngine* engineInstance = new flex::FlexEngine();
		engineInstance->Initialize();
		engineInstance->UpdateAndRender();
		delete engineInstance;
	}

	if (g_bShowConsole)
	{
		system("pause");
	}

	return 0;
#endif
}

#ifdef _WINDOWS
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	FLEX_UNUSED(hInstance);
	FLEX_UNUSED(hPrevInstance);
	FLEX_UNUSED(lpCmdLine);
	FLEX_UNUSED(nCmdShow);

	g_bShowConsole = false;

	return main(0, nullptr);
}
#endif
