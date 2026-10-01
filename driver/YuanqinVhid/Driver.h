#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#include "YuanqinVhidProtocol.h"

typedef struct _YUANQIN_DEVICE_CONTEXT {
    VHFHANDLE VhfHandle;
} YUANQIN_DEVICE_CONTEXT, *PYUANQIN_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(YUANQIN_DEVICE_CONTEXT, YuanqinGetDeviceContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD YuanqinEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP YuanqinEvtDeviceCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL YuanqinEvtIoDeviceControl;
