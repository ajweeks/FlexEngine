#include "stdafx.h"

#include <windows.h>

#if defined(DEBUG) | defined(_DEBUG)
	// Memory leak checking includes
	#define _CRTDBG_MAP_ALLOC  
	#include <stdlib.h>  
	#include <crtdbg.h>  
#endif

#include "TechDemo.h"

int main (int argc, char *argv[])
{
	// Notify user if heap is corrupt
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

	// Enable run-time memory leak check for debug builds
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(370);
#endif

	TechDemo* techDemo = new TechDemo();
	techDemo->Initialize();
	techDemo->UpdateAndRender();
	delete techDemo;

	exit(EXIT_SUCCESS);
}
