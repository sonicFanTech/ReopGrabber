#include "main.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <iterator>
#include <cstddef>
#include <cwctype>
#include <cstdint>
#include <shellapi.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// Keep the existing 1.0 version identifier unchanged.
static constexpr wchar_t APP_VERSION[] = L"1.0";
static constexpr wchar_t APP_USER_AGENT[] = L"ReopGrabber/1.0";
static constexpr wchar_t STARTUP_VALUE_NAME[] = L"ReopGrabber";
static constexpr wchar_t COPYRIGHT_LINE[] = L"Copyright (C) 2026 SHAPELESS GOAT STUDIOS. All rights reserved.";

static HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
static std::wstring gLastHttpError;
static std::wstring gLastPowerShellError;

static fs::path AppDir() {
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return fs::current_path();
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

static fs::path ExePath() {
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return fs::path(std::wstring(buffer.data(), length));
}

static std::wstring ExeData(const wchar_t* name) {
    return (AppDir() / name).wstring();
}

static void Color(WORD color) {
    if (hOut != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(hOut, color);
}

static void Line() {
    Color(8);
    std::wcout << L"  +--------------------------------------------------------------------+\n";
    Color(7);
}

static void Header() {
    system("cls");
    Color(11);
    std::wcout << L"\n  +--------------------------------------------------------------------+\n";
    std::wcout << L"  |                         REOPGRABBER                                |\n";
    std::wcout << L"  |                 GitHub Repository Monitor                          |\n";
    std::wcout << L"  +--------------------------------------------------------------------+\n";
    Color(8);
    std::wcout << L"  Version " << APP_VERSION << L"\n";
    std::wcout << L"  " << COPYRIGHT_LINE << L"\n";
    std::wcout << L"  ----------------------------------------------------------------------\n";
    Color(7);
}

static std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == value.npos) return L"";
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

static std::wstring ReadLine(const wchar_t* prompt) {
    Color(11);
    std::wcout << L"\n  " << prompt << L": ";
    Color(7);
    std::wstring value;
    std::getline(std::wcin, value);
    return Trim(value);
}

static std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

static bool ParseRepo(const std::wstring& input, Repository& repo) {
    std::wstring value = Trim(input);
    const std::wstring prefix = L"https://github.com/";
    if (Lower(value).rfind(prefix, 0) != 0) return false;

    std::wstring rest = value.substr(prefix.size());
    while (!rest.empty() && rest.back() == L'/') rest.pop_back();
    if (rest.size() > 4 && Lower(rest.substr(rest.size() - 4)) == L".git") rest.resize(rest.size() - 4);

    const auto slash = rest.find(L'/');
    if (slash == rest.npos) return false;
    const auto end = rest.find(L'/', slash + 1);
    repo.owner = rest.substr(0, slash);
    repo.name = rest.substr(slash + 1, end == rest.npos ? rest.size() - slash - 1 : end - slash - 1);

    if (repo.owner.empty() || repo.name.empty()) return false;
    if (repo.owner.find_first_of(L" ?#\\") != repo.owner.npos || repo.name.find_first_of(L" ?#\\") != repo.name.npos) return false;
    repo.url = prefix + repo.owner + L"/" + repo.name;
    return true;
}

static std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

static std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

static std::wstring UrlEncode(const std::wstring& value, bool preserveSlashes = false) {
    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    const std::string bytes = Utf8(value);
    std::wstring result;
    for (unsigned char ch : bytes) {
        const bool unreserved = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved || (preserveSlashes && ch == '/')) {
            result.push_back(static_cast<wchar_t>(ch));
        } else {
            result.push_back(L'%');
            result.push_back(hex[(ch >> 4) & 0x0F]);
            result.push_back(hex[ch & 0x0F]);
        }
    }
    return result;
}

static std::wstring JsonString(const std::wstring& json, const std::wstring& key) {
    const std::wstring needle = L"\"" + key + L"\"";
    auto position = json.find(needle);
    if (position == json.npos) return {};
    position = json.find(L':', position + needle.size());
    if (position == json.npos) return {};
    position = json.find(L'\"', position + 1);
    if (position == json.npos) return {};
    const auto end = json.find(L'\"', position + 1);
    if (end == json.npos) return {};
    return json.substr(position + 1, end - position - 1);
}

static bool HttpGet(const std::wstring& host, const std::wstring& path, std::vector<char>& data,
                    bool binary = false, DWORD* responseStatus = nullptr) {
    gLastHttpError.clear();
    if (responseStatus) *responseStatus = 0;

    HINTERNET session = WinHttpOpen(APP_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        gLastHttpError = L"WinHttpOpen failed (" + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        gLastHttpError = L"Could not connect to " + host + L" (" + std::to_wstring(GetLastError()) + L").";
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        gLastHttpError = L"Could not create HTTPS request (" + std::to_wstring(GetLastError()) + L").";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    // ZIP downloads can redirect from codeload; explicitly allow HTTPS redirects.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    const std::wstring headers = binary
        ? L"Accept: application/zip\r\n"
        : L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";

    bool ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);

    if (!ok) {
        gLastHttpError = L"HTTPS request failed (" + std::to_wstring(GetLastError()) + L").";
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (ok) {
        ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
        if (!ok) gLastHttpError = L"Could not read the HTTP response status (" + std::to_wstring(GetLastError()) + L").";
    }
    if (responseStatus) *responseStatus = status;
    if (ok && (status < 200 || status >= 300)) {
        gLastHttpError = L"GitHub returned HTTP " + std::to_wstring(status) + L".";
        ok = false;
    }

    if (ok) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                gLastHttpError = L"Could not read the response (" + std::to_wstring(GetLastError()) + L").";
                ok = false;
                break;
            }
            if (available == 0) break;
            const size_t oldSize = data.size();
            data.resize(oldSize + available);
            DWORD received = 0;
            if (!WinHttpReadData(request, data.data() + oldSize, available, &received)) {
                gLastHttpError = L"Download stream failed (" + std::to_wstring(GetLastError()) + L").";
                data.resize(oldSize);
                ok = false;
                break;
            }
            data.resize(oldSize + received);
            if (received == 0) break;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

static std::vector<Repository> LoadRepos() {
    std::vector<Repository> repositories;
    std::wifstream file(ExeData(L"repositories.txt"));
    std::wstring line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == L'#') continue;
        std::wistringstream stream(line);
        Repository repo;
        std::getline(stream, repo.url, L'\t');
        std::getline(stream, repo.destination, L'\t');
        std::getline(stream, repo.lastSha, L'\t');

        // Rebuild owner/name after loading: these fields are not stored separately in the list file.
        Repository parsed;
        if (!ParseRepo(repo.url, parsed)) continue;
        repo.url = parsed.url;
        repo.owner = parsed.owner;
        repo.name = parsed.name;
        if (!repo.url.empty() && !repo.destination.empty()) repositories.push_back(repo);
    }
    return repositories;
}

static bool SaveRepos(const std::vector<Repository>& repositories) {
    std::wofstream file(ExeData(L"repositories.txt"), std::ios::trunc);
    if (!file) return false;
    file << L"# GitHub URL<TAB>destination folder<TAB>last downloaded commit SHA\n";
    for (const auto& repo : repositories)
        file << repo.url << L'\t' << repo.destination << L'\t' << repo.lastSha << L'\n';
    return file.good();
}

static Settings LoadSettings() {
    Settings settings;
    std::wifstream file(ExeData(L"settings.ini"));
    std::wstring key;
    int value = 0;
    while (file >> key >> value) {
        if (key == L"IntervalMinutes") settings.intervalMinutes = std::clamp(value, 1, 10080);
        else if (key == L"AskBeforeDownload") settings.askBeforeDownload = value != 0;
        else if (key == L"StartInBackground") settings.startInBackground = value != 0;
        else if (key == L"RunAtStartup") settings.runAtStartup = value != 0;
    }
    return settings;
}

static bool SaveSettings(const Settings& settings) {
    std::wofstream file(ExeData(L"settings.ini"), std::ios::trunc);
    if (!file) return false;
    file << L"IntervalMinutes " << settings.intervalMinutes
         << L"\nAskBeforeDownload " << (settings.askBeforeDownload ? 1 : 0)
         << L"\nStartInBackground " << (settings.startInBackground ? 1 : 0)
         << L"\nRunAtStartup " << (settings.runAtStartup ? 1 : 0) << L'\n';
    return file.good();
}

static bool SetWindowsStartup(bool enabled) {
    HKEY key = nullptr;
    const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const LSTATUS openResult = RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (openResult != ERROR_SUCCESS) return false;

    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const fs::path executable = ExePath();
        if (executable.empty()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + executable.wstring() + L"\" --background";
        result = RegSetValueExW(key, STARTUP_VALUE_NAME, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, STARTUP_VALUE_NAME);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static bool ApplyStartupRegistration(const Settings& settings) {
    return SetWindowsStartup(settings.runAtStartup);
}

static bool GetLatest(Repository& repo) {
    std::vector<char> raw;
    const std::wstring apiBase = L"/repos/" + repo.owner + L"/" + repo.name;
    if (!HttpGet(L"api.github.com", apiBase, raw)) return false;

    const std::wstring metadata = Wide(std::string(raw.begin(), raw.end()));
    repo.branch = JsonString(metadata, L"default_branch");
    if (repo.branch.empty()) {
        gLastHttpError = L"GitHub did not return a default branch for this repository.";
        return false;
    }

    raw.clear();
    const std::wstring commitsPath = apiBase + L"/commits?per_page=1&sha=" + UrlEncode(repo.branch);
    if (!HttpGet(L"api.github.com", commitsPath, raw)) return false;
    const std::wstring commits = Wide(std::string(raw.begin(), raw.end()));
    const auto shaKey = commits.find(L"\"sha\"");
    if (shaKey == commits.npos) {
        gLastHttpError = L"The repository has no readable commit on its default branch.";
        return false;
    }
    auto valueStart = commits.find(L':', shaKey + 5);
    if (valueStart == commits.npos) return false;
    valueStart = commits.find(L'\"', valueStart + 1);
    if (valueStart == commits.npos) return false;
    const auto valueEnd = commits.find(L'\"', valueStart + 1);
    if (valueEnd == commits.npos) return false;
    repo.lastSha = commits.substr(valueStart + 1, valueEnd - valueStart - 1);
    return !repo.lastSha.empty();
}

static std::wstring PowerShellQuote(const std::wstring& value) {
    std::wstring quoted = L"'";
    for (wchar_t ch : value) {
        if (ch == L'\'') quoted += L"''";
        else quoted.push_back(ch);
    }
    quoted += L"'";
    return quoted;
}

static std::string Base64Encode(const unsigned char* data, size_t length) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((length + 2) / 3) * 4);
    for (size_t i = 0; i < length; i += 3) {
        const unsigned int a = data[i];
        const unsigned int b = (i + 1 < length) ? data[i + 1] : 0;
        const unsigned int c = (i + 2 < length) ? data[i + 2] : 0;
        const unsigned int triple = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(triple >> 18) & 0x3F]);
        output.push_back(alphabet[(triple >> 12) & 0x3F]);
        output.push_back(i + 1 < length ? alphabet[(triple >> 6) & 0x3F] : '=');
        output.push_back(i + 2 < length ? alphabet[triple & 0x3F] : '=');
    }
    return output;
}

static bool RunPowerShellEncoded(const std::wstring& script) {
    static_assert(sizeof(wchar_t) == 2, "PowerShell EncodedCommand requires Windows UTF-16 wchar_t.");
    const auto* bytes = reinterpret_cast<const unsigned char*>(script.data());
    const std::string encoded = Base64Encode(bytes, script.size() * sizeof(wchar_t));
    const std::wstring parameters = L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " +
        std::wstring(encoded.begin(), encoded.end());

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"open";
    executeInfo.lpFile = L"powershell.exe";
    executeInfo.lpParameters = parameters.c_str();
    const std::wstring workingDirectory = AppDir().wstring();
    executeInfo.lpDirectory = workingDirectory.c_str();
    executeInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&executeInfo) || !executeInfo.hProcess) {
        gLastPowerShellError = L"Could not start Windows PowerShell (Windows error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    const BOOL gotExitCode = GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);
    if (!gotExitCode) {
        gLastPowerShellError = L"Could not read the PowerShell exit code (Windows error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    if (exitCode != 0) {
        gLastPowerShellError = L"Windows PowerShell returned exit code " + std::to_wstring(exitCode) + L".";
        return false;
    }
    return true;
}

static bool IsZipFile(const std::vector<char>& data) {
    if (data.size() < 4) return false;
    return data[0] == 'P' && data[1] == 'K' &&
        ((static_cast<unsigned char>(data[2]) == 0x03 && static_cast<unsigned char>(data[3]) == 0x04) ||
         (static_cast<unsigned char>(data[2]) == 0x05 && static_cast<unsigned char>(data[3]) == 0x06) ||
         (static_cast<unsigned char>(data[2]) == 0x07 && static_cast<unsigned char>(data[3]) == 0x08));
}

static bool DownloadAndInstall(Repository& repo, const std::wstring& newSha) {
    if (repo.branch.empty()) {
        Color(12);
        std::wcout << L"    Cannot download: the default branch name is missing.\n";
        Color(7);
        return false;
    }

    std::error_code ec;
    fs::path destination;
    try {
        destination = fs::absolute(fs::path(repo.destination)).lexically_normal();
    } catch (const fs::filesystem_error&) {
        Color(12);
        std::wcout << L"    Invalid destination path (filesystem path conversion failed).\n";
        Color(7);
        return false;
    }

    if (destination.empty() || destination == destination.root_path() || destination.filename().empty()) {
        Color(12);
        std::wcout << L"    Choose a repository folder, not a drive root.\n";
        Color(7);
        return false;
    }

    const std::wstring uniqueSuffix = L"." + std::to_wstring(GetCurrentProcessId());
    fs::path stage = destination;
    stage += L".reopgrabber-stage" + uniqueSuffix;
    fs::path flat = destination;
    flat += L".reopgrabber-flat" + uniqueSuffix;
    fs::path backup = destination;
    backup += L".reopgrabber-backup" + uniqueSuffix;

    wchar_t tempBuffer[MAX_PATH + 2]{};
    const DWORD tempLength = GetTempPathW(MAX_PATH, tempBuffer);
    if (tempLength == 0 || tempLength >= MAX_PATH) {
        Color(12);
        std::wcout << L"    Could not locate the Windows temporary folder.\n";
        Color(7);
        return false;
    }
    fs::path zipFile = fs::path(tempBuffer) /
        (L"ReopGrabber_" + std::to_wstring(GetCurrentProcessId()) + L".zip");

    const std::wstring encodedBranch = UrlEncode(repo.branch, true);
    const std::wstring downloadPath = L"/" + repo.owner + L"/" + repo.name + L"/zip/refs/heads/" + encodedBranch;

    std::vector<char> zipData;
    DWORD httpStatus = 0;
    Color(11);
    std::wcout << L"    Downloading repository ZIP from GitHub...\n";
    Color(7);
    if (!HttpGet(L"codeload.github.com", downloadPath, zipData, true, &httpStatus)) {
        Color(12);
        std::wcout << L"    Download failed: " << (gLastHttpError.empty() ? L"unknown network error" : gLastHttpError) << L"\n";
        Color(7);
        return false;
    }
    if (!IsZipFile(zipData)) {
        Color(12);
        std::wcout << L"    GitHub returned a response that was not a valid ZIP archive (HTTP "
                   << httpStatus << L").\n";
        Color(7);
        return false;
    }

    std::wcout << L"    Download complete (" << (zipData.size() / (1024 * 1024)) << L" MB). Preparing extraction...\n";
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        Color(12);
        std::wcout << L"    Could not create destination parent folder (filesystem error " << ec.value() << L").\n";
        Color(7);
        return false;
    }

    {
        std::ofstream output(zipFile, std::ios::binary | std::ios::trunc);
        if (!output) {
            Color(12);
            std::wcout << L"    Could not write temporary ZIP file: " << zipFile.wstring() << L"\n";
            Color(7);
            return false;
        }
        output.write(zipData.data(), static_cast<std::streamsize>(zipData.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            fs::remove(zipFile, ec);
            Color(12);
            std::wcout << L"    Writing the downloaded ZIP failed.\n";
            Color(7);
            return false;
        }
    }

    fs::remove_all(stage, ec);
    ec.clear();
    fs::remove_all(flat, ec);
    ec.clear();

    fs::path extractErrorFile = zipFile;
    extractErrorFile += L".error.txt";
    gLastPowerShellError.clear();
    const std::wstring script =
        L"$ErrorActionPreference = 'Stop'; try { "
        L"$zip = " + PowerShellQuote(zipFile.wstring()) + L"; "
        L"$stage = " + PowerShellQuote(stage.wstring()) + L"; "
        L"$flat = " + PowerShellQuote(flat.wstring()) + L"; "
        L"$errorFile = " + PowerShellQuote(extractErrorFile.wstring()) + L"; "
        L"if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }; "
        L"if (Test-Path -LiteralPath $flat) { Remove-Item -LiteralPath $flat -Recurse -Force }; "
        L"Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force; "
        L"$items = @(Get-ChildItem -LiteralPath $stage -Force); "
        L"if ($items.Count -eq 1 -and $items[0].PSIsContainer) { "
        L"New-Item -ItemType Directory -Path $flat | Out-Null; "
        L"Get-ChildItem -LiteralPath $items[0].FullName -Force | Move-Item -Destination $flat -Force; "
        L"Remove-Item -LiteralPath $stage -Recurse -Force; "
        L"Move-Item -LiteralPath $flat -Destination $stage -Force; } "
        L"exit 0; } catch { [IO.File]::WriteAllText($errorFile, $_.Exception.ToString()); exit 1; }";

    Color(11);
    std::wcout << L"    Extracting ZIP archive...\n";
    Color(7);
    const bool extracted = RunPowerShellEncoded(script);
    fs::remove(zipFile, ec);

    if (!extracted || !fs::exists(stage)) {
        fs::remove_all(stage, ec);
        fs::remove_all(flat, ec);
        Color(12);
        std::wcout << L"    Extraction failed. The existing repository folder was not changed.\n";
        if (fs::exists(extractErrorFile)) {
            std::ifstream errorInput(extractErrorFile, std::ios::binary);
            std::string errorText((std::istreambuf_iterator<char>(errorInput)), std::istreambuf_iterator<char>());
            const std::wstring detail = Wide(errorText);
            if (!detail.empty()) std::wcout << L"    PowerShell: " << detail << L"\n";
        } else if (!gLastPowerShellError.empty()) {
            std::wcout << L"    " << gLastPowerShellError << L"\n";
        } else {
            std::wcout << L"    Confirm Windows PowerShell and the Expand-Archive command are available.\n";
        }
        fs::remove(extractErrorFile, ec);
        Color(7);
        return false;
    }
    fs::remove(extractErrorFile, ec);

    fs::remove_all(backup, ec);
    ec.clear();
    const bool hadOldInstall = fs::exists(destination);
    if (hadOldInstall) {
        fs::rename(destination, backup, ec);
        if (ec) {
            fs::remove_all(stage, ec);
            Color(12);
            std::wcout << L"    Could not move the old repository folder aside. Check permissions or close files using it.\n";
            Color(7);
            return false;
        }
    }

    ec.clear();
    fs::rename(stage, destination, ec);
    if (ec) {
        if (hadOldInstall) {
            std::error_code restoreError;
            fs::rename(backup, destination, restoreError);
        }
        fs::remove_all(stage, ec);
        Color(12);
        std::wcout << L"    Could not install the extracted repository. The old folder was restored when possible.\n";
        Color(7);
        return false;
    }

    if (hadOldInstall) fs::remove_all(backup, ec);
    repo.destination = destination.wstring();
    repo.lastSha = newSha;
    Color(10);
    std::wcout << L"    Repository installed successfully:\n      " << destination.wstring() << L"\n";
    Color(7);
    return true;
}

static bool CheckOne(Repository& repo, const Settings& settings) {
    Color(14);
    std::wcout << L"\n  Checking " << repo.owner << L"/" << repo.name << L"...\n";
    Color(7);

    Repository latest = repo;
    if (!GetLatest(latest)) {
        Color(12);
        std::wcout << L"    Could not check GitHub: " << (gLastHttpError.empty() ? L"unknown API error" : gLastHttpError) << L"\n";
        Color(7);
        return false;
    }

    const std::wstring latestSha = latest.lastSha;
    repo.branch = latest.branch;
    if (!repo.lastSha.empty() && latestSha == repo.lastSha) {
        Color(10);
        std::wcout << L"    Up to date.\n";
        Color(7);
        return true;
    }

    Color(13);
    std::wcout << L"    Update found! Latest commit: " << latestSha.substr(0, 12) << L"\n";
    Color(7);

    if (settings.askBeforeDownload) {
        const std::wstring answer = ReadLine(L"Download and replace the installed folder? [y/N]");
        if (Lower(answer) != L"y" && Lower(answer) != L"yes") {
            std::wcout << L"    Skipped. ReopGrabber will check this update again later.\n";
            return true;
        }
    }

    return DownloadAndInstall(repo, latestSha);
}

static void ListRepos(const std::vector<Repository>& repositories) {
    if (repositories.empty()) {
        std::wcout << L"\n  No repositories added yet.\n";
        return;
    }
    for (size_t i = 0; i < repositories.size(); ++i) {
        Color(11);
        std::wcout << L"\n  [" << (i + 1) << L"] " << repositories[i].url << L"\n";
        Color(7);
        std::wcout << L"      Folder: " << repositories[i].destination
                   << L"\n      Last installed SHA: "
                   << (repositories[i].lastSha.empty() ? L"(not downloaded yet)" : repositories[i].lastSha.substr(0, 12))
                   << L"\n";
    }
}

static void AddRepo(std::vector<Repository>& repositories) {
    Repository repo;
    if (!ParseRepo(ReadLine(L"Paste GitHub repository URL"), repo)) {
        Color(12);
        std::wcout << L"  Invalid URL. Expected https://github.com/owner/repository\n";
        Color(7);
        return;
    }
    for (const auto& existing : repositories) {
        if (Lower(existing.url) == Lower(repo.url)) {
            std::wcout << L"  That repository is already in the list.\n";
            return;
        }
    }

    const std::wstring destinationInput = ReadLine(L"Full folder path for extracted repository");
    if (destinationInput.empty()) {
        std::wcout << L"  Destination cannot be empty.\n";
        return;
    }
    try {
        repo.destination = fs::absolute(fs::path(destinationInput)).lexically_normal().wstring();
    } catch (const fs::filesystem_error&) {
        std::wcout << L"  That destination path is not valid.\n";
        return;
    }
    if (fs::path(repo.destination) == fs::path(repo.destination).root_path()) {
        std::wcout << L"  Please choose a folder beneath the drive root.\n";
        return;
    }

    repositories.push_back(repo);
    SaveRepos(repositories);
    Color(10);
    std::wcout << L"  Repository added.\n";
    Color(7);
}

static void RemoveRepo(std::vector<Repository>& repositories) {
    ListRepos(repositories);
    if (repositories.empty()) return;
    const auto selection = ReadLine(L"Entry number to remove");
    try {
        const size_t number = std::stoul(selection);
        if (number < 1 || number > repositories.size()) throw 1;
        repositories.erase(repositories.begin() + static_cast<std::ptrdiff_t>(number - 1));
        SaveRepos(repositories);
        Color(10);
        std::wcout << L"  Entry removed. Downloaded files were not deleted.\n";
        Color(7);
    } catch (...) {
        std::wcout << L"  Invalid selection.\n";
    }
}

static void SettingsMenu(Settings& settings) {
    for (;;) {
        Header();
        std::wcout << L"  SETTINGS\n";
        Line();
        std::wcout << L"  1. Check interval: " << settings.intervalMinutes << L" minutes\n";
        std::wcout << L"  2. Ask before downloading: " << (settings.askBeforeDownload ? L"Yes" : L"No (automatic)") << L"\n";
        std::wcout << L"  3. Start monitoring immediately when opened: " << (settings.startInBackground ? L"Yes" : L"No") << L"\n";
        std::wcout << L"  4. Run ReopGrabber when Windows starts: " << (settings.runAtStartup ? L"Enabled" : L"Disabled") << L"\n";
        std::wcout << L"  0. Back\n";

        const auto choice = ReadLine(L"Select setting");
        if (choice == L"0") break;
        if (choice == L"1") {
            try {
                settings.intervalMinutes = std::clamp(std::stoi(ReadLine(L"Minutes (1-10080)")), 1, 10080);
            } catch (...) { }
        } else if (choice == L"2") {
            settings.askBeforeDownload = !settings.askBeforeDownload;
        } else if (choice == L"3") {
            settings.startInBackground = !settings.startInBackground;
        } else if (choice == L"4") {
            const bool enable = !settings.runAtStartup;
            if (SetWindowsStartup(enable)) {
                settings.runAtStartup = enable;
                Color(10);
                std::wcout << (enable ? L"  Windows startup entry enabled.\n" : L"  Windows startup entry removed.\n");
                Color(7);
            } else {
                Color(12);
                std::wcout << L"  Could not update the Windows startup registry entry.\n";
                Color(7);
            }
        }
        SaveSettings(settings);
    }
}

static void Background(std::vector<Repository>& repositories, Settings& settings) {
    Header();
    Color(10);
    std::wcout << L"  Background monitoring is running. Press Ctrl+C to stop.\n";
    Color(7);
    std::wcout << L"  Check interval: " << settings.intervalMinutes << L" minute(s)\n";
    std::wcout << L"  Ask before download: " << (settings.askBeforeDownload ? L"Yes" : L"No") << L"\n";
    std::wcout << L"  Windows startup: " << (settings.runAtStartup ? L"Enabled" : L"Disabled") << L"\n";
    if (repositories.empty()) std::wcout << L"\n  No repositories are configured. Add repositories and restart monitoring.\n";

    for (;;) {
        for (auto& repo : repositories) {
            CheckOne(repo, settings);
            SaveRepos(repositories);
        }
        std::wcout << L"\n  Next check in " << settings.intervalMinutes << L" minute(s)...\n";
        for (int second = 0; second < settings.intervalMinutes * 60; ++second)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleTitleW(L"ReopGrabber - GitHub Repository Monitor");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (!fs::exists(ExeData(L"repositories.txt"))) {
        std::wofstream file(ExeData(L"repositories.txt"));
        file << L"# GitHub URL<TAB>destination folder<TAB>last downloaded commit SHA\n";
    }
    if (!fs::exists(ExeData(L"settings.ini"))) SaveSettings(Settings{});

    auto repositories = LoadRepos();
    auto settings = LoadSettings();
    ApplyStartupRegistration(settings);

    if (argc > 1 && std::wstring(argv[1]) == L"--background") {
        Background(repositories, settings);
        return 0;
    }
    if (settings.startInBackground) {
        Background(repositories, settings);
        return 0;
    }

    for (;;) {
        Header();
        Color(10);
        std::wcout << L"  " << repositories.size() << L" repository/repositories configured\n";
        Color(7);
        std::wcout << L"  Windows startup: " << (settings.runAtStartup ? L"Enabled" : L"Disabled") << L"\n";
        Line();
        std::wcout << L"  1. Add GitHub repository\n"
                      L"  2. View repositories\n"
                      L"  3. Check all repositories now\n"
                      L"  4. Remove repository from list\n"
                      L"  5. Settings\n"
                      L"  6. Start background monitoring\n"
                      L"  0. Exit\n";

        const auto choice = ReadLine(L"Select an option");
        if (choice == L"0") break;
        if (choice == L"1") AddRepo(repositories);
        else if (choice == L"2") {
            ListRepos(repositories);
            ReadLine(L"Press Enter to continue");
        } else if (choice == L"3") {
            for (auto& repo : repositories) {
                CheckOne(repo, settings);
                SaveRepos(repositories);
            }
            ReadLine(L"Press Enter to continue");
        } else if (choice == L"4") RemoveRepo(repositories);
        else if (choice == L"5") SettingsMenu(settings);
        else if (choice == L"6") Background(repositories, settings);
        else {
            std::wcout << L"  Unknown option.\n";
            Sleep(700);
        }
    }

    SaveRepos(repositories);
    SaveSettings(settings);
    return 0;
}
