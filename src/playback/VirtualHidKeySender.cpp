#include "yuanqin/playback/VirtualHidKeySender.h"

#include "YuanqinVhidProtocol.h"

#include <SetupAPI.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace yuanqin::playback {
namespace {

HANDLE openVirtualKeyboard(DWORD& error) noexcept {
    const HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_YUANQIN_VHID, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return INVALID_HANDLE_VALUE;
    }

    HANDLE result = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    if (SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVINTERFACE_YUANQIN_VHID,
                                    0, &interfaceData)) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0,
                                         &requiredSize, nullptr);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            requiredSize >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            std::vector<unsigned char> storage(requiredSize);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(*detail);
            if (SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail,
                                                 requiredSize, nullptr, nullptr)) {
                result = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
        }
    }

    error = result == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

unsigned char noteToHidUsage(char note) noexcept {
    const unsigned char value = static_cast<unsigned char>(note);
    const char upper = static_cast<char>(std::toupper(value));
    return upper >= 'A' && upper <= 'Z'
        ? static_cast<unsigned char>(0x04 + (upper - 'A'))
        : 0;
}

}  // namespace

VirtualHidKeySender::VirtualHidKeySender(unsigned short holdMilliseconds) noexcept
    : holdMilliseconds_(std::clamp<unsigned short>(holdMilliseconds, 1, 1000)) {
    handle_ = openVirtualKeyboard(lastError_);
    if (isOpen() && !sendCommand(YuanqinVhidCommandPing, {})) {
        close();
    }
}

VirtualHidKeySender::~VirtualHidKeySender() {
    releaseAll();
    close();
}

VirtualHidKeySender::VirtualHidKeySender(VirtualHidKeySender&& other) noexcept
    : handle_(other.handle_), lastError_(other.lastError_),
      holdMilliseconds_(other.holdMilliseconds_) {
    other.handle_ = INVALID_HANDLE_VALUE;
}

VirtualHidKeySender& VirtualHidKeySender::operator=(VirtualHidKeySender&& other) noexcept {
    if (this != &other) {
        releaseAll();
        close();
        handle_ = other.handle_;
        lastError_ = other.lastError_;
        holdMilliseconds_ = other.holdMilliseconds_;
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}

bool VirtualHidKeySender::isOpen() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

DWORD VirtualHidKeySender::lastError() const noexcept {
    return lastError_;
}

bool VirtualHidKeySender::sendKey(char note) {
    const unsigned char usage = noteToHidUsage(note);
    return usage != 0 && sendCommand(YuanqinVhidCommandTap, {&usage, 1});
}

bool VirtualHidKeySender::sendChord(std::span<const char> notes) {
    if (notes.empty() || notes.size() > YUANQIN_VHID_MAX_KEYS) {
        lastError_ = ERROR_INVALID_PARAMETER;
        return false;
    }

    std::array<unsigned char, YUANQIN_VHID_MAX_KEYS> usages{};
    std::size_t count = 0;
    for (const char note : notes) {
        const unsigned char usage = noteToHidUsage(note);
        if (usage == 0) {
            lastError_ = ERROR_INVALID_PARAMETER;
            return false;
        }
        if (std::find(usages.begin(), usages.begin() + static_cast<std::ptrdiff_t>(count), usage) ==
            usages.begin() + static_cast<std::ptrdiff_t>(count)) {
            usages[count++] = usage;
        }
    }
    return count != 0 && sendCommand(YuanqinVhidCommandTap, {usages.data(), count});
}

void VirtualHidKeySender::releaseAll() noexcept {
    if (isOpen()) {
        sendCommand(YuanqinVhidCommandReleaseAll, {});
    }
}

bool VirtualHidKeySender::sendCommand(unsigned char command,
                                      std::span<const unsigned char> usages) noexcept {
    if (!isOpen() || usages.size() > YUANQIN_VHID_MAX_KEYS) {
        lastError_ = isOpen() ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE;
        return false;
    }

    YUANQIN_VHID_COMMAND request{};
    request.Version = YUANQIN_VHID_PROTOCOL_VERSION;
    request.Command = command;
    request.KeyCount = static_cast<unsigned char>(usages.size());
    request.HoldMilliseconds = holdMilliseconds_;
    std::copy(usages.begin(), usages.end(), request.Keys);

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(handle_, IOCTL_YUANQIN_VHID_COMMAND,
                         &request, sizeof(request), nullptr, 0,
                         &bytesReturned, nullptr)) {
        lastError_ = GetLastError();
        return false;
    }
    lastError_ = ERROR_SUCCESS;
    return true;
}

void VirtualHidKeySender::close() noexcept {
    if (isOpen()) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

}  // namespace yuanqin::playback
