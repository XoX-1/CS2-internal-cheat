#pragma once
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace KillSound {
    void Run();
    void Reset();

    bool BrowseAndAddFiles(HWND owner);
    std::vector<std::string> GetFileList();
    int GetSelectedIndex();
    void SetSelectedIndex(int index);
    int GetAppliedIndex();
    bool ApplySelected();
}
