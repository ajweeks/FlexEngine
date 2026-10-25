#include "stdafx.hpp"

#include "FlexEngine.hpp"
#include "Platform/Platform.hpp"
#include "SceneTestRunner.hpp"
#include "Test.hpp"

// Memory leak checking includes
#if defined(DEBUG) && defined(_WINDOWS)
#define _CRTDBG_MAP_ALLOC
#include <heapapi.h>
#endif

bool g_bShowConsole = true;

int main(int argc, char *argv[])
{
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

	bool bRunUnitTests = argc >= 2 && strcmp(argv[1], "--test") == 0;
	bool bHeadless = argc >= 3 && strcmp(argv[2], "--headless") == 0;

	if (bRunUnitTests)
	{
		i32 result = flex::FlexTest::Run();

		if (!bHeadless)
		{
			// TODO: Use cross-platform solution here
			i32 pauseResult = system("pause");
			FLEX_UNUSED(pauseResult);
		}

		return result;
	}


	// Usage: Flex --test-scenes [--frames=N] [--screenshots] [--profile] [scene_file_name ...]
	// Loads each scene (or only those listed) in a hidden window and exits with the number of
	// scenes which printed any warnings or errors while loading/simulating
	bool bTestScenes = argc >= 2 && strcmp(argv[1], "--test-scenes") == 0;
	std::vector<std::string> sceneTestFileNames;
	u32 sceneTestFrameCount = 10;
	bool bSceneTestScreenshots = false;
	bool bSceneTestProfile = false;
	if (bTestScenes)
	{
		for (i32 i = 2; i < argc; ++i)
		{
			if (strncmp(argv[i], "--frames=", 9) == 0)
			{
				sceneTestFrameCount = (u32)atoi(argv[i] + 9);
			}
			else if (strcmp(argv[i], "--screenshots") == 0)
			{
				// Saves the last frame of each scene to saved/screenshots/
				bSceneTestScreenshots = true;
			}
			else if (strcmp(argv[i], "--profile") == 0)
			{
				// Prints CPU & GPU timings for the second half of each scene's frames
				bSceneTestProfile = true;
			}
			else
			{
				sceneTestFileNames.emplace_back(argv[i]);
			}
		}

		flex::g_bHeadless = true;
	}

	i32 result = 0;
	bool bGameMode = false;

	{
		// Created before the engine so that warnings during startup are captured too
		flex::SceneTestRunner* sceneTestRunner = bTestScenes ? new flex::SceneTestRunner(sceneTestFileNames, sceneTestFrameCount, bSceneTestScreenshots, bSceneTestProfile) : nullptr;

		// The game's main menu is shown on startup, unless testing or "--editor" is passed (which opens the last
		// opened test scene)
		bool bEditor = false;
		for (i32 i = 1; i < argc; ++i)
		{
			bEditor = bEditor || strcmp(argv[i], "--editor") == 0;
		}

		flex::FlexEngine* engineInstance = new flex::FlexEngine();
		engineInstance->SetSceneTestRunner(sceneTestRunner);
		bGameMode = !bTestScenes && !bRunUnitTests && !bEditor;
		engineInstance->SetGameMode(bGameMode);
		engineInstance->Initialize();
		engineInstance->UpdateAndRender();
		delete engineInstance;

		if (sceneTestRunner != nullptr)
		{
			// Print after shutdown so the summary is the last thing in the output
			sceneTestRunner->PrintSummary();
			result = sceneTestRunner->GetFailureCount();
			delete sceneTestRunner;
		}
	}

	// Keeps the console open to read the output (but quitting the game shouldn't leave a console waiting)
	if (g_bShowConsole && !flex::g_bHeadless && !bGameMode)
	{
		i32 pauseResult = system("pause");
		FLEX_UNUSED(pauseResult);
	}

	return result;
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
