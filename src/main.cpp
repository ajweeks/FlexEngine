#include "stdafx.h"

#include "TechDemo.h"

#include <windows.h>

// Memory leak checking includes
#if defined(DEBUG) | defined(_DEBUG)
	#define _CRTDBG_MAP_ALLOC  
	#include <stdlib.h>  
	#include <crtdbg.h>  
#endif

int main(int argc, char *argv[])
{
	// Notify user if heap is corrupt
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

	// Enable run-time memory leak check for debug builds
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(430);
#endif

	TechDemo* techDemo = new TechDemo();
	techDemo->Initialize();
	techDemo->UpdateAndRender();
	delete techDemo;

	system("PAUSE");
	exit(EXIT_SUCCESS);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	return main(0, {});
}
