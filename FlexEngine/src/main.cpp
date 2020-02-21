#include "stdafx.hpp"

#include "FlexEngine.hpp"
#include "Test.hpp"

// Memory leak checking includes
#if defined(DEBUG)
#define _CRTDBG_MAP_ALLOC
#endif

bool g_bShowConsole = true;

int main(int argc, char *argv[])
{
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);

#ifdef _WIN32
	// Enable run-time memory leak check for debug builds
#if defined(DEBUG)
	// Notify user if heap is corrupt
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(47947);
#endif
#endif

	flex::InitializeLogger();

#if RUN_UNIT_TESTS
	flex::FlexTest::Run();

	system("PAUSE");
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
		system("PAUSE");
	}

	return 0;
#endif
}

#ifdef _WIN32
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	g_bShowConsole = false;

	return main(0, nullptr);
}
#endif
