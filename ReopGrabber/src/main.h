#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

struct Repository {
    std::wstring url;
    std::wstring destination;
    std::wstring lastSha;
    std::wstring owner;
    std::wstring name;
    std::wstring branch;
};

struct Settings {
    int intervalMinutes = 30;
    bool askBeforeDownload = true;
    bool startInBackground = false;
    bool runAtStartup = false;
};
