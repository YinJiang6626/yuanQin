#pragma once

// This header is shared by the Win32 client and the KMDF driver. Include
// Windows.h in user mode, or ntddk.h/wdf.h in kernel mode, before this file.

#define YUANQIN_VHID_PROTOCOL_VERSION 1UL
#define YUANQIN_VHID_MAX_KEYS 6U

// {3A3ACB6A-8DA4-4628-BEA4-211A89D2416C}
static const GUID GUID_DEVINTERFACE_YUANQIN_VHID = {
    0x3a3acb6a, 0x8da4, 0x4628, {0xbe, 0xa4, 0x21, 0x1a, 0x89, 0xd2, 0x41, 0x6c}};

#define IOCTL_YUANQIN_VHID_COMMAND \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)

typedef enum _YUANQIN_VHID_COMMAND_KIND {
    YuanqinVhidCommandPing = 0,
    YuanqinVhidCommandTap = 1,
    YuanqinVhidCommandReleaseAll = 2
} YUANQIN_VHID_COMMAND_KIND;

#pragma pack(push, 1)
typedef struct _YUANQIN_VHID_COMMAND {
    ULONG Version;
    UCHAR Command;
    UCHAR Modifier;
    UCHAR KeyCount;
    UCHAR Reserved;
    USHORT HoldMilliseconds;
    USHORT Reserved2;
    UCHAR Keys[YUANQIN_VHID_MAX_KEYS];
    UCHAR Reserved3[2];
} YUANQIN_VHID_COMMAND, *PYUANQIN_VHID_COMMAND;
#pragma pack(pop)
