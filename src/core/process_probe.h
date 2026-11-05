#pragma once
#include <string>
#include <vector>

// True if a process with any of these executable names is running. Names only:
// nothing opens the processes it finds.
bool IsProcessRunning(const std::vector<std::wstring>& exeNames);
