#include "yuanqin/playback/GenshinWindowTarget.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace yuanqin::playback {
namespace {

struct SearchResult {
    HWND processMatch{nullptr};
    HWND titleMatch{nullptr};
};

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    return value;
}

std::wstring processName(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) {
        return {};
    }

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return {};
    }

    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    const bool queried = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
    CloseHandle(process);
    if (!queried || length == 0) {
        return {};
    }

    std::wstring name(path.data(), length);
    const auto separator = name.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        name.erase(0, separator + 1);
    }
    return lower(std::move(name));
}

std::wstring windowTitle(HWND window) {
    std::array<wchar_t, 512> title{};
    const int length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    return length > 0 ? std::wstring(title.data(), static_cast<std::size_t>(length))
                      : std::wstring{};
}

BOOL CALLBACK enumerateWindows(HWND window, LPARAM parameter) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    auto& result = *reinterpret_cast<SearchResult*>(parameter);
    const std::wstring name = processName(window);
    if (name == L"yuanshen.exe" || name == L"genshinimpact.exe" ||
        name == L"genshinimpactcloudgame.exe") {
        result.processMatch = window;
        return FALSE;
    }

    const std::wstring title = lower(windowTitle(window));
    if (!result.titleMatch &&
        (title == L"原神" || title == L"genshin impact" ||
         title.find(L"云·原神") != std::wstring::npos)) {
        result.titleMatch = window;
    }
    return TRUE;
}

}  // namespace

HWND GenshinWindowTarget::find() noexcept {
    SearchResult result;
    EnumWindows(enumerateWindows, reinterpret_cast<LPARAM>(&result));
    return result.processMatch ? result.processMatch : result.titleMatch;
}

bool GenshinWindowTarget::activate(HWND overlayWindow) noexcept {
    const HWND target = find();
    if (!target) {
        return false;
    }

    return activate(target, overlayWindow);
}

bool GenshinWindowTarget::activate(HWND target, HWND overlayWindow) noexcept {
    if (!target || !IsWindow(target)) {
        return false;
    }

    if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
    }

    const DWORD currentThread = GetCurrentThreadId();
    const DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr)
        : 0;

    const bool attachedTarget = targetThread != 0 && targetThread != currentThread &&
                                AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
    const bool attachedForeground = foregroundThread != 0 &&
                                    foregroundThread != currentThread &&
                                    foregroundThread != targetThread &&
                                    AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;

    BringWindowToTop(target);
    const bool activated = SetForegroundWindow(target) != FALSE;
    SetFocus(target);

    if (attachedForeground) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
    if (attachedTarget) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }

    if (overlayWindow) {
        SetWindowPos(overlayWindow, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    return activated || GetForegroundWindow() == target;
}

}  // namespace yuanqin::playback
