#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Windows command-line quoting, including quotes and trailing backslashes.
std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

DWORD run(const fs::path &exe, const std::vector<std::wstring> &args, bool hidden) {
    std::wstring command = quote(exe.wstring());
    for (const auto &arg : args) command += L" " + quote(arg);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hidden) { startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE; }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                        hidden ? CREATE_NO_WINDOW : 0, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("Could not start the packaged application or Windows tar.exe.");
    // Pass file-open activation permission through to the application, which
    // may in turn forward the request to an already running instance.
    if (!hidden) AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return code;
}

int WINAPI WinMain(HINSTANCE module, HINSTANCE, LPSTR, int) {
    fs::path temporary;
    bool ownsTemporary = false;
    try {
        wchar_t tempPath[MAX_PATH + 1];
        const DWORD length = GetTempPathW(MAX_PATH + 1, tempPath);
        if (!length || length > MAX_PATH) throw std::runtime_error("Could not locate the temporary directory.");
        GUID id{};
        wchar_t idText[40];
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, idText, 40))
            throw std::runtime_error("Could not create a temporary directory name.");
        temporary = fs::path(tempPath) / (std::wstring(L"DocumentViewer-") + idText);
        if (!fs::create_directory(temporary)) throw std::runtime_error("Could not create the temporary directory.");
        ownsTemporary = true;
        auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(10));
        const DWORD size = resource ? SizeofResource(module, resource) : 0;
        const void *data = resource ? LockResource(LoadResource(module, resource)) : nullptr;
        if (!size || !data) throw std::runtime_error("The application payload is missing.");
        const auto archive = temporary / L"payload.zip";
        {
            std::ofstream output(archive, std::ios::binary);
            output.write(static_cast<const char *>(data), size);
            if (!output) throw std::runtime_error("Could not write the application payload.");
        }
        wchar_t systemPath[MAX_PATH + 1];
        if (!GetSystemDirectoryW(systemPath, MAX_PATH + 1)) throw std::runtime_error("Could not locate Windows tar.exe.");
        if (run(fs::path(systemPath) / L"tar.exe", {L"-xf", archive.wstring(), L"-C", temporary.wstring()}, true))
            throw std::runtime_error("Could not unpack the application. Windows 10 (1803+) or Windows 11 is required.");
        fs::remove(archive);
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args;
        for (int i = 1; argv && i < argc; ++i) args.emplace_back(argv[i]);
        if (argv) LocalFree(argv);
        const DWORD code = run(temporary / L"bin" / L"DocumentViewer.exe", args, false);
        // Only this launcher's newly created directory is removed.
        std::error_code ignored;
        fs::remove_all(temporary, ignored);
        return static_cast<int>(code);
    } catch (const std::exception &error) {
        if (ownsTemporary) {
            std::error_code ignored;
            fs::remove_all(temporary, ignored);
        }
        MessageBoxA(nullptr, error.what(), "Document Viewer", MB_OK | MB_ICONERROR);
        return 1;
    }
}
