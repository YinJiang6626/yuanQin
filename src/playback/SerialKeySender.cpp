#include "yuanqin/playback/SerialKeySender.h"

#include <Windows.h>

#include <cctype>

namespace yuanqin::playback {
namespace {

HANDLE toHandle(void* value) noexcept {
    return static_cast<HANDLE>(value);
}

char toFirmwareKey(char note) noexcept {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(note)))) {
        case 'Z': return 'A'; case 'X': return 'S'; case 'C': return 'D';
        case 'V': return 'F'; case 'B': return 'G'; case 'N': return 'H'; case 'M': return 'J';
        case 'A': return 'Q'; case 'S': return 'W'; case 'D': return 'E';
        case 'F': return 'R'; case 'G': return 'T'; case 'H': return 'Y'; case 'J': return 'U';
        case 'Q': return '1'; case 'W': return '2'; case 'E': return '3';
        case 'R': return '4'; case 'T': return '5'; case 'Y': return '6'; case 'U': return '7';
        default: return '\0';
    }
}

}  // namespace

SerialKeySender::SerialKeySender(std::string_view portName) {
    const std::string devicePath = "\\\\.\\" + std::string(portName);
    const HANDLE handle = CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    DCB configuration{};
    configuration.DCBlength = sizeof(configuration);
    if (!GetCommState(handle, &configuration)) {
        CloseHandle(handle);
        return;
    }

    configuration.BaudRate = CBR_115200;
    configuration.ByteSize = 8;
    configuration.StopBits = ONESTOPBIT;
    configuration.Parity = NOPARITY;
    if (!SetCommState(handle, &configuration)) {
        CloseHandle(handle);
        return;
    }

    handle_ = handle;
}

SerialKeySender::~SerialKeySender() {
    if (isOpen()) {
        CloseHandle(toHandle(handle_));
    }
}

bool SerialKeySender::isOpen() const noexcept {
    return handle_ != nullptr && toHandle(handle_) != INVALID_HANDLE_VALUE;
}

bool SerialKeySender::sendKey(char note) {
    const char firmwareKey = toFirmwareKey(note);
    if (!isOpen() || firmwareKey == '\0') {
        return false;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(toHandle(handle_), &firmwareKey, 1, &bytesWritten, nullptr) || bytesWritten != 1) {
        return false;
    }

    // The existing firmware consumes a one-byte keyboard report at a time.
    // Keep the original protection interval so chords are not dropped over USB serial.
    Sleep(40);
    return true;
}

}  // namespace yuanqin::playback
