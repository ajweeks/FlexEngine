
// TODO: Make this file OS agnostic
#include <windows.h>

#include "TechDemo.h"

int main (int argc, char *argv[])
{
	TechDemo* techDemo = new TechDemo();
	techDemo->Initialize();
	techDemo->UpdateAndRun();
	delete techDemo;

	exit(EXIT_SUCCESS);
}
