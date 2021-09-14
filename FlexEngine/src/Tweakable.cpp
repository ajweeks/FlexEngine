#include "stdafx.hpp"

#include "Tweakable.hpp"

#include "Helpers.hpp"

namespace flex
{
	static std::vector<FileWatcher*> fileWatcherers;

	i32 AddTweakableWatch(Tweakable* tweakable, const char* filePath)
	{

		// Store previous tweakables registered in the same translation unit
		// So files aren't unnecessarily parsed multiple times
		static Tweakable* previousTweakable = nullptr;

		if (previousTweakable == nullptr)
		{
			FileWatcher* watcher = new FileWatcher(filePath, (void*)tweakable);

			fileWatcherers.push_back(watcher);
		}
		else
		{
			// Hook into previous tweakable's linked list so this tweakable also get updated
			previousTweakable->next = tweakable;
			previousTweakable = tweakable;
		}

		// TODO: Use?
		return 0;
	}

	void UpdateTweakables()
	{
		for (FileWatcher* watcher : fileWatcherers)
		{
			if (watcher->Update())
			{
				std::string fileContents;
				if (ReadFile(watcher->filePath, fileContents, false))
				{
					std::vector<std::string> fileLines = SplitNoStrip(fileContents, '\n');
					Tweakable* tweakable = (Tweakable*)watcher->directoryWatcher.userData;
					while (tweakable != nullptr)
					{
						std::string lineStr = fileLines[tweakable->lineNumber - 1];
						const char* tweakValueStart = "TWEAKABLE(";
						size_t valueStart = lineStr.find(tweakValueStart);
						if (valueStart != std::string::npos)
						{
							size_t valueEnd = lineStr.find(")", valueStart);
							if (valueEnd != std::string::npos)
							{
								valueStart += strlen(tweakValueStart);
								std::string valueStr = lineStr.substr(valueStart, valueEnd - valueStart);
								std::string prevTweakableValueStr = tweakable->data.ToString();
								if (tweakable->data.SetValueFromString(valueStr.c_str()))
								{
									std::string newTweakableValueStr = tweakable->data.ToString();
									Print("Changed tweakable value from %s to %s\n", prevTweakableValueStr.c_str(), newTweakableValueStr.c_str());
								}
								else
								{
									PrintError("Failed to change tweakable value on line %u, line contents:\n\t%s\n", tweakable->lineNumber, lineStr.c_str());
								}
							}
						}

						tweakable = tweakable->next;
					}
				}
				else
				{
					PrintError("Failed to read tweakable source file at %s\n", watcher->filePath);
				}
			}
		}
	}

	void DestroyTweakables()
	{
		for (FileWatcher* watcher : fileWatcherers)
		{
			delete watcher;
		}

		fileWatcherers.clear();
	}

	FileWatcher::FileWatcher(const char* filePath, void* userData) :
		filePath(filePath),
		directoryWatcher(ExtractDirectoryString(filePath), true)
	{
		directoryWatcher.userData = userData;
	}

	bool FileWatcher::Update()
	{
		return directoryWatcher.Update();
	}
} // namespace flex
