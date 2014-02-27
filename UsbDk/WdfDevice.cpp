#include "WdfDevice.h"
#include "trace.h"
#include "WdfDevice.tmh"

bool CDeviceInit::Create(_In_ WDFDRIVER Driver, _In_ CONST UNICODE_STRING &SDDLString)
{
    auto DeviceInit = WdfControlDeviceInitAllocate(Driver, &SDDLString);
    if (DeviceInit == nullptr)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! Cannot allocate DeviceInit");
        return false;
    }

    Attach(DeviceInit);

    return true;
}

CDeviceInit::~CDeviceInit()
{
    if (m_Attached)
    {
        Free();
    }
}

void CDeviceInit::Attach(PWDFDEVICE_INIT DevInit)
{
    m_Attached = true;
    CPreAllocatedDeviceInit::Attach(DevInit);
}

PWDFDEVICE_INIT CDeviceInit::Detach()
{
    m_Attached = false;
    return CPreAllocatedDeviceInit::Detach();
}

NTSTATUS CPreAllocatedDeviceInit::SetName(const UNICODE_STRING &Name)
{
    auto status = WdfDeviceInitAssignName(m_DeviceInit, &Name);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! failed %!STATUS!", status);
    }

    return status;
}

void CPreAllocatedDeviceInit::Free()
{
    WdfDeviceInitFree(m_DeviceInit);
}

void CPreAllocatedDeviceInit::Attach(PWDFDEVICE_INIT DeviceInit)
{
    m_DeviceInit = DeviceInit;
}

PWDFDEVICE_INIT CPreAllocatedDeviceInit::Detach()
{
    auto DevInit = m_DeviceInit;
    m_DeviceInit = nullptr;
    return DevInit;
}

NTSTATUS CPreAllocatedDeviceInit::SetPreprocessCallback(PFN_WDFDEVICE_WDM_IRP_PREPROCESS Callback, UCHAR MajorFunction, UCHAR MinorFunction)
{
    auto status = WdfDeviceInitAssignWdmIrpPreprocessCallback(m_DeviceInit, Callback, MajorFunction, &MinorFunction, 1);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! status: %!STATUS!", status);
    }
    return status;
}

NTSTATUS CWdfDevice::CreateSymLink(const UNICODE_STRING &Name)
{
    auto status = WdfDeviceCreateSymbolicLink(m_Device, &Name);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! failed %!STATUS!", status);
    }
    return status;
}

NTSTATUS CWdfDevice::Create(CPreAllocatedDeviceInit &DeviceInit, WDF_OBJECT_ATTRIBUTES &DeviceAttr)
{
    auto DevInitObj = DeviceInit.Detach();

    auto status = WdfDeviceCreate(&DevInitObj, &DeviceAttr, &m_Device);
    if (!NT_SUCCESS(status))
    {
        DeviceInit.Attach(DevInitObj);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! failed %!STATUS!", status);
    }
    return status;
}

NTSTATUS CWdfQueue::Create()
{
    WDF_IO_QUEUE_CONFIG      QueueConfig;
    WDFQUEUE                 Queue;
    WDF_OBJECT_ATTRIBUTES    Attributes;

    InitConfig(QueueConfig);
    SetCallbacks(QueueConfig);

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;

    auto status = m_OwnerDevice.AddQueue(QueueConfig, Attributes, Queue);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! failed %!STATUS!", status);
    }

    return status;
}

NTSTATUS CWdfDevice::AddQueue(WDF_IO_QUEUE_CONFIG &Config, WDF_OBJECT_ATTRIBUTES &Attributes, WDFQUEUE &Queue)
{
    auto status = WdfIoQueueCreate(m_Device, &Config, &Attributes, &Queue);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_WDFDEVICE, "%!FUNC! failed %!STATUS!", status);
    }

    return status;
}

void CWdfDefaultQueue::InitConfig(WDF_IO_QUEUE_CONFIG &QueueConfig)
{
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, m_DispatchType);
}