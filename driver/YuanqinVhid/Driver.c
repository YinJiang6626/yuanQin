#include "Driver.h"

// Standard six-key keyboard report descriptor. It intentionally declares only
// keyboard input and LED output; there is no vendor feature channel in the HID
// child device. Control commands use the parent device interface instead.
static UCHAR YuanqinKeyboardReportDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (No event)
    0x29, 0x65,        //   Usage Maximum (Keyboard Application)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0               // End Collection
};

#pragma pack(push, 1)
typedef struct _YUANQIN_KEYBOARD_REPORT {
    UCHAR Modifier;
    UCHAR Reserved;
    UCHAR Keys[YUANQIN_VHID_MAX_KEYS];
} YUANQIN_KEYBOARD_REPORT, *PYUANQIN_KEYBOARD_REPORT;
#pragma pack(pop)

static NTSTATUS YuanqinSubmitReport(
    _In_ PYUANQIN_DEVICE_CONTEXT DeviceContext,
    _In_ const YUANQIN_KEYBOARD_REPORT* Report
)
{
    HID_XFER_PACKET packet;

    if (DeviceContext->VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = (PUCHAR)Report;
    packet.reportBufferLen = sizeof(*Report);
    packet.reportId = 0;
    return VhfReadReportSubmit(DeviceContext->VhfHandle, &packet);
}

static NTSTATUS YuanqinReleaseAll(_In_ PYUANQIN_DEVICE_CONTEXT DeviceContext)
{
    YUANQIN_KEYBOARD_REPORT report;
    RtlZeroMemory(&report, sizeof(report));
    return YuanqinSubmitReport(DeviceContext, &report);
}

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, YuanqinEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath,
                           WDF_NO_OBJECT_ATTRIBUTES, &config,
                           WDF_NO_HANDLE);
}

NTSTATUS YuanqinEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    VHF_CONFIG vhfConfig;
    PYUANQIN_DEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, YUANQIN_DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = YuanqinEvtDeviceCleanup;
    deviceAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    deviceContext = YuanqinGetDeviceContext(device);
    deviceContext->VhfHandle = NULL;

    status = WdfDeviceCreateDeviceInterface(
        device, &GUID_DEVINTERFACE_YUANQIN_VHID, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = YuanqinEvtIoDeviceControl;
    status = WdfIoQueueCreate(device, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    VHF_CONFIG_INIT(&vhfConfig,
                    WdfDeviceWdmGetDeviceObject(device),
                    (USHORT)sizeof(YuanqinKeyboardReportDescriptor),
                    YuanqinKeyboardReportDescriptor);
    vhfConfig.VersionNumber = 0x0100;

    status = VhfCreate(&vhfConfig, &deviceContext->VhfHandle);
    if (!NT_SUCCESS(status)) {
        deviceContext->VhfHandle = NULL;
        return status;
    }

    status = VhfStart(deviceContext->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(deviceContext->VhfHandle, TRUE);
        deviceContext->VhfHandle = NULL;
        return status;
    }

    return STATUS_SUCCESS;
}

VOID YuanqinEvtDeviceCleanup(_In_ WDFOBJECT DeviceObject)
{
    PYUANQIN_DEVICE_CONTEXT deviceContext;

    PAGED_CODE();
    deviceContext = YuanqinGetDeviceContext((WDFDEVICE)DeviceObject);
    if (deviceContext->VhfHandle != NULL) {
        (void)YuanqinReleaseAll(deviceContext);
        VhfDelete(deviceContext->VhfHandle, TRUE);
        deviceContext->VhfHandle = NULL;
    }
}

VOID YuanqinEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status;
    WDFDEVICE device;
    PYUANQIN_DEVICE_CONTEXT deviceContext;
    PYUANQIN_VHID_COMMAND command;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    PAGED_CODE();

    if (IoControlCode != IOCTL_YUANQIN_VHID_COMMAND) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(YUANQIN_VHID_COMMAND),
        (PVOID*)&command, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    if (command->Version != YUANQIN_VHID_PROTOCOL_VERSION) {
        WdfRequestComplete(Request, STATUS_REVISION_MISMATCH);
        return;
    }

    device = WdfIoQueueGetDevice(Queue);
    deviceContext = YuanqinGetDeviceContext(device);

    switch ((YUANQIN_VHID_COMMAND_KIND)command->Command) {
        case YuanqinVhidCommandPing:
            status = STATUS_SUCCESS;
            break;

        case YuanqinVhidCommandReleaseAll:
            status = YuanqinReleaseAll(deviceContext);
            break;

        case YuanqinVhidCommandTap: {
            YUANQIN_KEYBOARD_REPORT report;
            YUANQIN_KEYBOARD_REPORT releasedReport;
            LARGE_INTEGER delay;
            UCHAR index;

            if (command->KeyCount == 0 ||
                command->KeyCount > YUANQIN_VHID_MAX_KEYS ||
                command->HoldMilliseconds == 0 ||
                command->HoldMilliseconds > 1000) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            RtlZeroMemory(&report, sizeof(report));
            report.Modifier = command->Modifier;
            for (index = 0; index < command->KeyCount; ++index) {
                // The current score language permits A-Z only.
                if (command->Keys[index] < 0x04 || command->Keys[index] > 0x1D) {
                    status = STATUS_INVALID_PARAMETER;
                    goto CompleteRequest;
                }
                report.Keys[index] = command->Keys[index];
            }

            status = YuanqinSubmitReport(deviceContext, &report);
            if (!NT_SUCCESS(status)) {
                break;
            }

            delay.QuadPart = -((LONGLONG)command->HoldMilliseconds * 10 * 1000);
            (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);

            RtlZeroMemory(&releasedReport, sizeof(releasedReport));
            status = YuanqinSubmitReport(deviceContext, &releasedReport);
            break;
        }

        default:
            status = STATUS_INVALID_PARAMETER;
            break;
    }

CompleteRequest:
    WdfRequestComplete(Request, status);
}
