/**********************************************************************
* Copyright (c) 2013-2014  Red Hat, Inc.
*
* Developed by Daynix Computing LTD.
*
* Authors:
*     Dmitry Fleytman <dmitry@daynix.com>
*     Pavel Gurvich <pavel@daynix.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
**********************************************************************/

#pragma once

#include "WdfDevice.h"
#include "Alloc.h"
#include "UsbDkUtil.h"
#include "FilterDevice.h"

typedef struct tag_USB_DK_DEVICE_ID USB_DK_DEVICE_ID;
typedef struct tag_USB_DK_DEVICE_INFO USB_DK_DEVICE_INFO;
typedef struct tag_USB_DK_CONFIG_DESCRIPTOR_REQUEST USB_DK_CONFIG_DESCRIPTOR_REQUEST;
class CUsbDkFilterDevice;
class CWdfRequest;

class CUsbDkControlDeviceQueue : public CWdfDefaultQueue, public CAllocatable<PagedPool, 'QCHR'>
{
public:
    CUsbDkControlDeviceQueue(CWdfDevice &Device, WDF_IO_QUEUE_DISPATCH_TYPE DispatchType)
        : CWdfDefaultQueue(Device, DispatchType)
    {}

private:
    virtual void SetCallbacks(WDF_IO_QUEUE_CONFIG &QueueConfig) override;
    static void DeviceControl(WDFQUEUE Queue,
                              WDFREQUEST Request,
                              size_t OutputBufferLength,
                              size_t InputBufferLength,
                              ULONG IoControlCode);

    static void CountDevices(CWdfRequest &Request, WDFQUEUE Queue);
    static void EnumerateDevices(CWdfRequest &Request, WDFQUEUE Queue);
    static void GetConfigurationDescriptor(CWdfRequest &Request, WDFQUEUE Queue);
    static void AddRedirect(CWdfRequest &Request, WDFQUEUE Queue);
    static void RemoveRedirect(CWdfRequest &Request, WDFQUEUE Queue);

    typedef NTSTATUS(CUsbDkControlDevice::*USBDevControlMethod)(const USB_DK_DEVICE_ID&);
    static void DoUSBDeviceOp(CWdfRequest &Request, WDFQUEUE Queue, USBDevControlMethod Method);

    template <typename TInputObj, typename TOutputObj>
    using USBDevControlMethodWithOutput = NTSTATUS(CUsbDkControlDevice::*)(const TInputObj& Input,
                                                                           TOutputObj *Output,
                                                                           size_t* OutputBufferLen);
    template <typename TInputObj, typename TOutputObj>
    static void DoUSBDeviceOp(CWdfRequest &Request,
                              WDFQUEUE Queue,
                              USBDevControlMethodWithOutput<TInputObj, TOutputObj> Method);

    CUsbDkControlDeviceQueue(const CUsbDkControlDeviceQueue&) = delete;
    CUsbDkControlDeviceQueue& operator= (const CUsbDkControlDeviceQueue&) = delete;
};

class CUsbDkRedirection : public CAllocatable<NonPagedPool, 'NRHR'>, public CWdmRefCountingObject
{
public:
    enum : ULONG
    {
        NO_REDIRECTOR = (ULONG) -1,
    };

    NTSTATUS Create(const USB_DK_DEVICE_ID &Id);

    bool operator==(const USB_DK_DEVICE_ID &Id) const;
    bool operator==(const CUsbDkChildDevice &Dev) const;
    bool operator==(const CUsbDkRedirection &Other) const;

    void Dump() const;

    void NotifyRedirectorCreated(ULONG RedirectorID);
    void NotifyRedirectionRemoved()
    { m_RedirectionRemoved.Set(); }
    void NotifyRedirectionRemovalStarted();

    bool IsRedirected() const
    { return m_RedirectorID != NO_REDIRECTOR; }

    bool IsPreparedForRemove() const
    { return m_RemovalInProgress; }

    NTSTATUS WaitForAttachment()
    { return m_RedirectionCreated.Wait(true, -SecondsTo100Nanoseconds(120)); }

    bool WaitForDetachment();

    ULONG RedirectorID()
    { return m_RedirectorID; }

protected:
    virtual void OnLastReferenceGone()
    { delete this; }

private:
    CString m_DeviceID;
    CString m_InstanceID;

    CWdmEvent m_RedirectionCreated;
    CWdmEvent m_RedirectionRemoved;
    ULONG m_RedirectorID = NO_REDIRECTOR;

    bool m_RemovalInProgress = false;

    DECLARE_CWDMLIST_ENTRY(CUsbDkRedirection);
};

class CUsbDkControlDevice : private CWdfControlDevice, public CAllocatable<NonPagedPool, 'DCHR'>
{
public:
    CUsbDkControlDevice() {}

    NTSTATUS Create(WDFDRIVER Driver);
    void RegisterFilter(CUsbDkFilterDevice &FilterDevice)
    { m_FilterDevices.PushBack(&FilterDevice); }
    void UnregisterFilter(CUsbDkFilterDevice &FilterDevice)
    { m_FilterDevices.Remove(&FilterDevice); }

    ULONG CountDevices();
    bool EnumerateDevices(USB_DK_DEVICE_INFO *outBuff, size_t numberAllocatedDevices, size_t &numberExistingDevices);
    NTSTATUS ResetUsbDevice(const USB_DK_DEVICE_ID &DeviceId);
    NTSTATUS AddRedirect(const USB_DK_DEVICE_ID &DeviceId, PULONG RedirectorID, size_t *OutputBuffLen);
    NTSTATUS RemoveRedirect(const USB_DK_DEVICE_ID &DeviceId);
    NTSTATUS GetConfigurationDescriptor(const USB_DK_CONFIG_DESCRIPTOR_REQUEST &Request,
                                        PUSB_CONFIGURATION_DESCRIPTOR Descriptor,
                                        size_t *OutputBuffLen);

    static bool Allocate();
    static void Deallocate()
    { delete m_UsbDkControlDevice; }
    static CUsbDkControlDevice* Reference(WDFDRIVER Driver);
    static void Release()
    { m_UsbDkControlDevice->Release(); }

    template <typename TDevID>
    bool ShouldRedirect(const TDevID &Dev) const
    {
        bool DontRedirect = true;
        const_cast<RedirectionsSet*>(&m_Redirections)->ModifyOne(&Dev, [&DontRedirect](CUsbDkRedirection *Entry)
                                            { DontRedirect = Entry->IsRedirected() || Entry->IsPreparedForRemove(); });
        return !DontRedirect;
    }

    template <typename TDevID>
    void NotifyRedirectionRemoved(const TDevID &Dev) const
    {
        const_cast<RedirectionsSet*>(&m_Redirections)->ModifyOne(&Dev, [](CUsbDkRedirection *Entry)
                                                                       { Entry->NotifyRedirectionRemoved();} );
   }

    bool NotifyRedirectorAttached(CRegText *DeviceID, CRegText *InstanceID, ULONG RedrectorID);
    bool NotifyRedirectorRemovalStarted(const USB_DK_DEVICE_ID &ID);
    bool WaitForDetachment(const USB_DK_DEVICE_ID &ID);

private:
    CObjHolder<CUsbDkControlDeviceQueue> m_DeviceQueue;
    static CRefCountingHolder<CUsbDkControlDevice> *m_UsbDkControlDevice;

    CWdmList<CUsbDkFilterDevice, CLockedAccess, CNonCountingObject> m_FilterDevices;

    typedef CWdmSet<CUsbDkRedirection, CLockedAccess, CNonCountingObject> RedirectionsSet;
    RedirectionsSet m_Redirections;

    template <typename TPredicate, typename TFunctor>
    bool UsbDevicesForEachIf(TPredicate Predicate, TFunctor Functor)
    { return m_FilterDevices.ForEach([&](CUsbDkFilterDevice* Dev){ return Dev->EnumerateChildrenIf(Predicate, Functor); }); }

    template <typename TFunctor>
    bool EnumUsbDevicesByID(const USB_DK_DEVICE_ID &ID, TFunctor Functor);
    PDEVICE_OBJECT GetPDOByDeviceID(const USB_DK_DEVICE_ID &DeviceID);

    bool UsbDeviceExists(const USB_DK_DEVICE_ID &ID);

    static void ContextCleanup(_In_ WDFOBJECT DeviceObject);
    NTSTATUS AddDeviceToSet(const USB_DK_DEVICE_ID &DeviceId, CUsbDkRedirection **NewRedirection);
    void AddRedirectRollBack(const USB_DK_DEVICE_ID &DeviceId, bool WithReset);

    NTSTATUS GetUsbDeviceConfigurationDescriptor(const USB_DK_DEVICE_ID &DeviceID,
                                                 UCHAR DescriptorIndex,
                                                 USB_CONFIGURATION_DESCRIPTOR &Descriptor,
                                                 size_t Length);
};

typedef struct _USBDK_CONTROL_DEVICE_EXTENSION {

    CUsbDkControlDevice *UsbDkControl; // Store your control data here

    _USBDK_CONTROL_DEVICE_EXTENSION(const _USBDK_CONTROL_DEVICE_EXTENSION&) = delete;
    _USBDK_CONTROL_DEVICE_EXTENSION& operator= (const _USBDK_CONTROL_DEVICE_EXTENSION&) = delete;

} USBDK_CONTROL_DEVICE_EXTENSION, *PUSBDK_CONTROL_DEVICE_EXTENSION;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(USBDK_CONTROL_DEVICE_EXTENSION, UsbDkControlGetContext);
