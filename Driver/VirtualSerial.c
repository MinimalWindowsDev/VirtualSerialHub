/*******************************************************************************
 * VirtualSerial.c - Kernel-Mode Virtual Serial Port Pair Driver (WDM)
 * 
 * Creates a null-modem pair: \Device\VirtualSerial0 <-> \Device\VirtualSerial1
 * Data written to one port is readable from the other (bidirectional pipe).
 * 
 * Part of VirtualSerialHub - FLOSS alternative to com0com
 * License: MIT
 ******************************************************************************/

#include <ntddk.h>
#include <ntstrsafe.h>

/* ============================================================================
 * Configuration & Constants
 * ============================================================================ */

#define VSERIAL_POOL_TAG        'lrsV'
#define VSERIAL_BUFFER_SIZE     4096
#define VSERIAL_NUM_PORTS       2

/* Device names */
#define DEVICE_NAME_0           L"\\Device\\VirtualSerial0"
#define DEVICE_NAME_1           L"\\Device\\VirtualSerial1"
#define SYMLINK_NAME_0          L"\\DosDevices\\VCOM0"
#define SYMLINK_NAME_1          L"\\DosDevices\\VCOM1"

/* IOCTL codes for serial port emulation (subset of standard serial IOCTLs) */
#define IOCTL_SERIAL_GET_BAUD_RATE      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 20, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_BAUD_RATE      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_LINE_CONTROL   CTL_CODE(FILE_DEVICE_SERIAL_PORT, 21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_LINE_CONTROL   CTL_CODE(FILE_DEVICE_SERIAL_PORT, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_TIMEOUTS       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_TIMEOUTS       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_CHARS          CTL_CODE(FILE_DEVICE_SERIAL_PORT, 24, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_CHARS          CTL_CODE(FILE_DEVICE_SERIAL_PORT, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_HANDFLOW       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 23, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_HANDFLOW       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_MODEMSTATUS    CTL_CODE(FILE_DEVICE_SERIAL_PORT, 26, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_COMMSTATUS     CTL_CODE(FILE_DEVICE_SERIAL_PORT, 27, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_PURGE              CTL_CODE(FILE_DEVICE_SERIAL_PORT, 19, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_QUEUE_SIZE     CTL_CODE(FILE_DEVICE_SERIAL_PORT, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_DTR            CTL_CODE(FILE_DEVICE_SERIAL_PORT, 9, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_CLR_DTR            CTL_CODE(FILE_DEVICE_SERIAL_PORT, 10, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_RTS            CTL_CODE(FILE_DEVICE_SERIAL_PORT, 12, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_CLR_RTS            CTL_CODE(FILE_DEVICE_SERIAL_PORT, 13, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_BREAK_ON       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_BREAK_OFF      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_WAIT_MASK      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 16, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_WAIT_MASK      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 17, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_WAIT_ON_MASK       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 18, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_PROPERTIES     CTL_CODE(FILE_DEVICE_SERIAL_PORT, 25, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Modem status bits */
#define SERIAL_MSR_CTS          0x10
#define SERIAL_MSR_DSR          0x20
#define SERIAL_MSR_RI           0x40
#define SERIAL_MSR_DCD          0x80

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Serial port configuration (emulated) */
typedef struct _SERIAL_CONFIG {
    ULONG   BaudRate;
    UCHAR   DataBits;
    UCHAR   Parity;
    UCHAR   StopBits;
    ULONG   Timeouts[5];    /* ReadInterval, ReadTotalMultiplier, ReadTotalConstant, 
                               WriteTotalMultiplier, WriteTotalConstant */
    ULONG   WaitMask;
    BOOLEAN DtrState;
    BOOLEAN RtsState;
} SERIAL_CONFIG, *PSERIAL_CONFIG;

/* Ring buffer for data transfer between ports */
typedef struct _RING_BUFFER {
    PUCHAR  Buffer;
    ULONG   Size;
    ULONG   Head;           /* Write position */
    ULONG   Tail;           /* Read position */
    ULONG   Count;          /* Bytes in buffer */
    KSPIN_LOCK Lock;
} RING_BUFFER, *PRING_BUFFER;

/* Per-port device extension */
typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT  Self;
    PDEVICE_OBJECT  Peer;           /* The other port in the pair */
    ULONG           PortIndex;      /* 0 or 1 */
    UNICODE_STRING  DeviceName;
    UNICODE_STRING  SymbolicLink;
    BOOLEAN         SymLinkCreated;
    
    SERIAL_CONFIG   Config;
    RING_BUFFER     RxBuffer;       /* Data received (written by peer) */
    
    /* Pending read IRP handling */
    PIRP            PendingReadIrp;
    KSPIN_LOCK      ReadLock;
    KDPC            ReadDpc;
    KTIMER          ReadTimer;
    
    /* Statistics */
    ULONG           BytesWritten;
    ULONG           BytesRead;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/* ============================================================================
 * Global Variables
 * ============================================================================ */

PDEVICE_OBJECT g_DeviceObjects[VSERIAL_NUM_PORTS] = { NULL, NULL };
PDRIVER_OBJECT g_DriverObject = NULL;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

DRIVER_INITIALIZE           DriverEntry;
DRIVER_UNLOAD               VSerialUnload;
DRIVER_DISPATCH             VSerialCreate;
DRIVER_DISPATCH             VSerialClose;
DRIVER_DISPATCH             VSerialRead;
DRIVER_DISPATCH             VSerialWrite;
DRIVER_DISPATCH             VSerialDeviceControl;
DRIVER_DISPATCH             VSerialCleanup;
KDEFERRED_ROUTINE           VSerialReadDpc;
DRIVER_CANCEL               VSerialCancelRead;

NTSTATUS CreateVirtualPort(PDRIVER_OBJECT DriverObject, ULONG PortIndex);
VOID     DestroyVirtualPort(ULONG PortIndex);
NTSTATUS RingBufferInit(PRING_BUFFER Ring, ULONG Size);
VOID     RingBufferFree(PRING_BUFFER Ring);
ULONG    RingBufferWrite(PRING_BUFFER Ring, PUCHAR Data, ULONG Length);
ULONG    RingBufferRead(PRING_BUFFER Ring, PUCHAR Data, ULONG MaxLength);
ULONG    RingBufferBytesAvailable(PRING_BUFFER Ring);
VOID     TryCompleteRead(PDEVICE_EXTENSION DevExt);

/* ============================================================================
 * Ring Buffer Implementation
 * ============================================================================ */

NTSTATUS RingBufferInit(PRING_BUFFER Ring, ULONG Size)
{
    Ring->Buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, Size, VSERIAL_POOL_TAG);
    if (!Ring->Buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Ring->Size = Size;
    Ring->Head = 0;
    Ring->Tail = 0;
    Ring->Count = 0;
    KeInitializeSpinLock(&Ring->Lock);
    
    return STATUS_SUCCESS;
}

VOID RingBufferFree(PRING_BUFFER Ring)
{
    if (Ring->Buffer) {
        ExFreePoolWithTag(Ring->Buffer, VSERIAL_POOL_TAG);
        Ring->Buffer = NULL;
    }
}

ULONG RingBufferWrite(PRING_BUFFER Ring, PUCHAR Data, ULONG Length)
{
    KIRQL OldIrql;
    ULONG Written = 0;
    ULONG ToWrite;
    
    KeAcquireSpinLock(&Ring->Lock, &OldIrql);
    
    while (Written < Length && Ring->Count < Ring->Size) {
        ToWrite = min(Length - Written, Ring->Size - Ring->Count);
        ToWrite = min(ToWrite, Ring->Size - Ring->Head);
        
        RtlCopyMemory(Ring->Buffer + Ring->Head, Data + Written, ToWrite);
        
        Ring->Head = (Ring->Head + ToWrite) % Ring->Size;
        Ring->Count += ToWrite;
        Written += ToWrite;
    }
    
    KeReleaseSpinLock(&Ring->Lock, OldIrql);
    return Written;
}

ULONG RingBufferRead(PRING_BUFFER Ring, PUCHAR Data, ULONG MaxLength)
{
    KIRQL OldIrql;
    ULONG Read = 0;
    ULONG ToRead;
    
    KeAcquireSpinLock(&Ring->Lock, &OldIrql);
    
    while (Read < MaxLength && Ring->Count > 0) {
        ToRead = min(MaxLength - Read, Ring->Count);
        ToRead = min(ToRead, Ring->Size - Ring->Tail);
        
        RtlCopyMemory(Data + Read, Ring->Buffer + Ring->Tail, ToRead);
        
        Ring->Tail = (Ring->Tail + ToRead) % Ring->Size;
        Ring->Count -= ToRead;
        Read += ToRead;
    }
    
    KeReleaseSpinLock(&Ring->Lock, OldIrql);
    return Read;
}

ULONG RingBufferBytesAvailable(PRING_BUFFER Ring)
{
    KIRQL OldIrql;
    ULONG Count;
    
    KeAcquireSpinLock(&Ring->Lock, &OldIrql);
    Count = Ring->Count;
    KeReleaseSpinLock(&Ring->Lock, OldIrql);
    
    return Count;
}

/* ============================================================================
 * Port Creation / Destruction
 * ============================================================================ */

NTSTATUS CreateVirtualPort(PDRIVER_OBJECT DriverObject, ULONG PortIndex)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject = NULL;
    PDEVICE_EXTENSION DevExt = NULL;
    UNICODE_STRING DeviceName;
    UNICODE_STRING SymbolicLink;
    
    /* Select names based on port index */
    if (PortIndex == 0) {
        RtlInitUnicodeString(&DeviceName, DEVICE_NAME_0);
        RtlInitUnicodeString(&SymbolicLink, SYMLINK_NAME_0);
    } else {
        RtlInitUnicodeString(&DeviceName, DEVICE_NAME_1);
        RtlInitUnicodeString(&SymbolicLink, SYMLINK_NAME_1);
    }
    
    /* Create the device object */
    Status = IoCreateDevice(
        DriverObject,
        sizeof(DEVICE_EXTENSION),
        &DeviceName,
        FILE_DEVICE_SERIAL_PORT,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &DeviceObject
    );
    
    if (!NT_SUCCESS(Status)) {
        DbgPrint("VirtualSerial: Failed to create device %lu: 0x%X\n", PortIndex, Status);
        return Status;
    }
    
    /* Initialize device extension */
    DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(DevExt, sizeof(DEVICE_EXTENSION));
    
    DevExt->Self = DeviceObject;
    DevExt->PortIndex = PortIndex;
    DevExt->DeviceName = DeviceName;
    DevExt->SymbolicLink = SymbolicLink;
    
    /* Initialize serial config with defaults */
    DevExt->Config.BaudRate = 9600;
    DevExt->Config.DataBits = 8;
    DevExt->Config.Parity = 0;      /* None */
    DevExt->Config.StopBits = 0;    /* 1 stop bit */
    DevExt->Config.DtrState = FALSE;
    DevExt->Config.RtsState = FALSE;
    
    /* Initialize receive ring buffer */
    Status = RingBufferInit(&DevExt->RxBuffer, VSERIAL_BUFFER_SIZE);
    if (!NT_SUCCESS(Status)) {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    
    /* Initialize read handling */
    KeInitializeSpinLock(&DevExt->ReadLock);
    KeInitializeTimer(&DevExt->ReadTimer);
    KeInitializeDpc(&DevExt->ReadDpc, VSerialReadDpc, DevExt);
    DevExt->PendingReadIrp = NULL;
    
    /* Create symbolic link */
    Status = IoCreateSymbolicLink(&SymbolicLink, &DeviceName);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("VirtualSerial: Failed to create symlink for port %lu: 0x%X\n", PortIndex, Status);
        RingBufferFree(&DevExt->RxBuffer);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DevExt->SymLinkCreated = TRUE;
    
    /* Use buffered I/O */
    DeviceObject->Flags |= DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    
    g_DeviceObjects[PortIndex] = DeviceObject;
    
    DbgPrint("VirtualSerial: Created port %lu (%wZ -> %wZ)\n", 
             PortIndex, &DeviceName, &SymbolicLink);
    
    return STATUS_SUCCESS;
}

VOID DestroyVirtualPort(ULONG PortIndex)
{
    PDEVICE_OBJECT DeviceObject = g_DeviceObjects[PortIndex];
    PDEVICE_EXTENSION DevExt;
    
    if (!DeviceObject) return;
    
    DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    
    /* Cancel any pending timer */
    KeCancelTimer(&DevExt->ReadTimer);
    
    /* Delete symbolic link */
    if (DevExt->SymLinkCreated) {
        IoDeleteSymbolicLink(&DevExt->SymbolicLink);
    }
    
    /* Free ring buffer */
    RingBufferFree(&DevExt->RxBuffer);
    
    /* Delete device */
    IoDeleteDevice(DeviceObject);
    g_DeviceObjects[PortIndex] = NULL;
    
    DbgPrint("VirtualSerial: Destroyed port %lu\n", PortIndex);
}

/* ============================================================================
 * Driver Entry / Unload
 * ============================================================================ */

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS Status;
    ULONG i;
    
    UNREFERENCED_PARAMETER(RegistryPath);
    
    DbgPrint("VirtualSerial: DriverEntry\n");
    
    g_DriverObject = DriverObject;
    
    /* Set up dispatch routines */
    DriverObject->DriverUnload = VSerialUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = VSerialCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = VSerialClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = VSerialRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = VSerialWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = VSerialDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = VSerialCleanup;
    
    /* Create both ports */
    for (i = 0; i < VSERIAL_NUM_PORTS; i++) {
        Status = CreateVirtualPort(DriverObject, i);
        if (!NT_SUCCESS(Status)) {
            /* Cleanup any created ports */
            while (i > 0) {
                DestroyVirtualPort(--i);
            }
            return Status;
        }
    }
    
    /* Link the ports as peers */
    ((PDEVICE_EXTENSION)g_DeviceObjects[0]->DeviceExtension)->Peer = g_DeviceObjects[1];
    ((PDEVICE_EXTENSION)g_DeviceObjects[1]->DeviceExtension)->Peer = g_DeviceObjects[0];
    
    DbgPrint("VirtualSerial: Driver loaded successfully\n");
    return STATUS_SUCCESS;
}

VOID VSerialUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    ULONG i;
    
    UNREFERENCED_PARAMETER(DriverObject);
    
    DbgPrint("VirtualSerial: Unloading driver\n");
    
    /* Destroy all ports */
    for (i = 0; i < VSERIAL_NUM_PORTS; i++) {
        DestroyVirtualPort(i);
    }
    
    DbgPrint("VirtualSerial: Driver unloaded\n");
}

/* ============================================================================
 * IRP Handlers
 * ============================================================================ */

NTSTATUS VSerialCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    
    DbgPrint("VirtualSerial: Port %lu opened\n", DevExt->PortIndex);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

NTSTATUS VSerialClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    
    DbgPrint("VirtualSerial: Port %lu closed\n", DevExt->PortIndex);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

NTSTATUS VSerialCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    KIRQL OldIrql;
    PIRP PendingIrp;
    
    /* Cancel any pending read */
    KeAcquireSpinLock(&DevExt->ReadLock, &OldIrql);
    PendingIrp = DevExt->PendingReadIrp;
    DevExt->PendingReadIrp = NULL;
    KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
    
    if (PendingIrp) {
        KeCancelTimer(&DevExt->ReadTimer);
        PendingIrp->IoStatus.Status = STATUS_CANCELLED;
        PendingIrp->IoStatus.Information = 0;
        IoCompleteRequest(PendingIrp, IO_NO_INCREMENT);
    }
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

/* Read cancellation routine */
VOID VSerialCancelRead(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    KIRQL OldIrql;
    
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    
    KeAcquireSpinLock(&DevExt->ReadLock, &OldIrql);
    if (DevExt->PendingReadIrp == Irp) {
        DevExt->PendingReadIrp = NULL;
    }
    KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
    
    KeCancelTimer(&DevExt->ReadTimer);
    
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/* DPC for completing reads */
VOID VSerialReadDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeferredContext;
    
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    
    TryCompleteRead(DevExt);
}

/* Try to complete a pending read if data is available */
VOID TryCompleteRead(PDEVICE_EXTENSION DevExt)
{
    KIRQL OldIrql;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PUCHAR Buffer;
    ULONG BytesRead;
    ULONG BytesRequested;
    
    KeAcquireSpinLock(&DevExt->ReadLock, &OldIrql);
    
    Irp = DevExt->PendingReadIrp;
    if (!Irp) {
        KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
        return;
    }
    
    /* Check if data is available */
    if (RingBufferBytesAvailable(&DevExt->RxBuffer) == 0) {
        KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
        return;
    }
    
    /* Clear pending IRP */
    DevExt->PendingReadIrp = NULL;
    
    /* Remove cancel routine */
    if (IoSetCancelRoutine(Irp, NULL) == NULL) {
        /* IRP is being cancelled, let cancel routine handle it */
        KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
        return;
    }
    
    KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
    
    KeCancelTimer(&DevExt->ReadTimer);
    
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BytesRequested = IoStack->Parameters.Read.Length;
    Buffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    
    /* Read available data */
    BytesRead = RingBufferRead(&DevExt->RxBuffer, Buffer, BytesRequested);
    DevExt->BytesRead += BytesRead;
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = BytesRead;
    IoCompleteRequest(Irp, IO_SERIAL_INCREMENT);
}

NTSTATUS VSerialRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG BytesRequested = IoStack->Parameters.Read.Length;
    PUCHAR Buffer;
    ULONG BytesRead;
    KIRQL OldIrql;
    LARGE_INTEGER Timeout;
    
    if (BytesRequested == 0) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
    
    Buffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    
    /* Try to read immediately if data available */
    BytesRead = RingBufferRead(&DevExt->RxBuffer, Buffer, BytesRequested);
    
    if (BytesRead > 0) {
        DevExt->BytesRead += BytesRead;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = BytesRead;
        IoCompleteRequest(Irp, IO_SERIAL_INCREMENT);
        return STATUS_SUCCESS;
    }
    
    /* No data available - pend the IRP */
    KeAcquireSpinLock(&DevExt->ReadLock, &OldIrql);
    
    /* Check if another read is already pending */
    if (DevExt->PendingReadIrp != NULL) {
        KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_BUSY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_BUSY;
    }
    
    /* Set up cancellation */
    IoSetCancelRoutine(Irp, VSerialCancelRead);
    if (Irp->Cancel) {
        if (IoSetCancelRoutine(Irp, NULL) != NULL) {
            KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
            Irp->IoStatus.Status = STATUS_CANCELLED;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_CANCELLED;
        }
    }
    
    /* Mark IRP pending */
    IoMarkIrpPending(Irp);
    DevExt->PendingReadIrp = Irp;
    
    KeReleaseSpinLock(&DevExt->ReadLock, OldIrql);
    
    /* Set a timeout (use ReadTotalTimeoutConstant if set, else 1 second default) */
    Timeout.QuadPart = -10000000LL;  /* 1 second in 100ns units (negative = relative) */
    if (DevExt->Config.Timeouts[2] > 0) {
        Timeout.QuadPart = -((LONGLONG)DevExt->Config.Timeouts[2] * 10000);
    }
    
    KeSetTimer(&DevExt->ReadTimer, Timeout, &DevExt->ReadDpc);
    
    return STATUS_PENDING;
}

NTSTATUS VSerialWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PDEVICE_EXTENSION PeerExt;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG BytesToWrite = IoStack->Parameters.Write.Length;
    PUCHAR Buffer;
    ULONG BytesWritten;
    
    if (BytesToWrite == 0) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
    
    /* Check if peer exists */
    if (!DevExt->Peer) {
        Irp->IoStatus.Status = STATUS_PORT_DISCONNECTED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PORT_DISCONNECTED;
    }
    
    Buffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    PeerExt = (PDEVICE_EXTENSION)DevExt->Peer->DeviceExtension;
    
    /* Write to peer's receive buffer (null modem: our TX -> peer's RX) */
    BytesWritten = RingBufferWrite(&PeerExt->RxBuffer, Buffer, BytesToWrite);
    DevExt->BytesWritten += BytesWritten;
    
    /* Try to complete any pending read on the peer */
    TryCompleteRead(PeerExt);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = BytesWritten;
    IoCompleteRequest(Irp, IO_SERIAL_INCREMENT);
    
    return STATUS_SUCCESS;
}

NTSTATUS VSerialDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    PDEVICE_EXTENSION DevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;
    PVOID InputBuffer = Irp->AssociatedIrp.SystemBuffer;
    PVOID OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = IoStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Information = 0;
    PDEVICE_EXTENSION PeerExt;
    ULONG ModemStatus;
    
    switch (IoControlCode) {
    
    case IOCTL_SERIAL_GET_BAUD_RATE:
        if (OutputLength >= sizeof(ULONG)) {
            *(PULONG)OutputBuffer = DevExt->Config.BaudRate;
            Information = sizeof(ULONG);
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_SET_BAUD_RATE:
        if (InputLength >= sizeof(ULONG)) {
            DevExt->Config.BaudRate = *(PULONG)InputBuffer;
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_GET_LINE_CONTROL:
        if (OutputLength >= 3) {
            PUCHAR Out = (PUCHAR)OutputBuffer;
            Out[0] = DevExt->Config.StopBits;
            Out[1] = DevExt->Config.Parity;
            Out[2] = DevExt->Config.DataBits;
            Information = 3;
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_SET_LINE_CONTROL:
        if (InputLength >= 3) {
            PUCHAR In = (PUCHAR)InputBuffer;
            DevExt->Config.StopBits = In[0];
            DevExt->Config.Parity = In[1];
            DevExt->Config.DataBits = In[2];
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_GET_TIMEOUTS:
        if (OutputLength >= sizeof(DevExt->Config.Timeouts)) {
            RtlCopyMemory(OutputBuffer, DevExt->Config.Timeouts, sizeof(DevExt->Config.Timeouts));
            Information = sizeof(DevExt->Config.Timeouts);
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_SET_TIMEOUTS:
        if (InputLength >= sizeof(DevExt->Config.Timeouts)) {
            RtlCopyMemory(DevExt->Config.Timeouts, InputBuffer, sizeof(DevExt->Config.Timeouts));
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_GET_MODEMSTATUS:
        /* Return modem status based on peer's DTR/RTS -> our DSR/CTS */
        if (OutputLength >= sizeof(ULONG)) {
            ModemStatus = 0;
            if (DevExt->Peer) {
                PeerExt = (PDEVICE_EXTENSION)DevExt->Peer->DeviceExtension;
                /* Null modem: peer DTR -> our DSR/DCD, peer RTS -> our CTS */
                if (PeerExt->Config.DtrState) {
                    ModemStatus |= SERIAL_MSR_DSR | SERIAL_MSR_DCD;
                }
                if (PeerExt->Config.RtsState) {
                    ModemStatus |= SERIAL_MSR_CTS;
                }
            }
            *(PULONG)OutputBuffer = ModemStatus;
            Information = sizeof(ULONG);
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_SET_DTR:
        DevExt->Config.DtrState = TRUE;
        break;
        
    case IOCTL_SERIAL_CLR_DTR:
        DevExt->Config.DtrState = FALSE;
        break;
        
    case IOCTL_SERIAL_SET_RTS:
        DevExt->Config.RtsState = TRUE;
        break;
        
    case IOCTL_SERIAL_CLR_RTS:
        DevExt->Config.RtsState = FALSE;
        break;
        
    case IOCTL_SERIAL_GET_WAIT_MASK:
        if (OutputLength >= sizeof(ULONG)) {
            *(PULONG)OutputBuffer = DevExt->Config.WaitMask;
            Information = sizeof(ULONG);
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_SET_WAIT_MASK:
        if (InputLength >= sizeof(ULONG)) {
            DevExt->Config.WaitMask = *(PULONG)InputBuffer;
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_PURGE:
        /* Clear buffers - we only have RX buffer */
        {
            KIRQL OldIrql;
            KeAcquireSpinLock(&DevExt->RxBuffer.Lock, &OldIrql);
            DevExt->RxBuffer.Head = 0;
            DevExt->RxBuffer.Tail = 0;
            DevExt->RxBuffer.Count = 0;
            KeReleaseSpinLock(&DevExt->RxBuffer.Lock, OldIrql);
        }
        break;
        
    case IOCTL_SERIAL_SET_QUEUE_SIZE:
    case IOCTL_SERIAL_SET_BREAK_ON:
    case IOCTL_SERIAL_SET_BREAK_OFF:
    case IOCTL_SERIAL_GET_CHARS:
    case IOCTL_SERIAL_SET_CHARS:
    case IOCTL_SERIAL_GET_HANDFLOW:
    case IOCTL_SERIAL_SET_HANDFLOW:
    case IOCTL_SERIAL_WAIT_ON_MASK:
        /* Stub - accept but do nothing */
        break;
        
    case IOCTL_SERIAL_GET_COMMSTATUS:
        if (OutputLength >= 18) {
            /* SERIAL_STATUS structure */
            RtlZeroMemory(OutputBuffer, 18);
            /* Set AmountInInQueue */
            ((PULONG)OutputBuffer)[2] = RingBufferBytesAvailable(&DevExt->RxBuffer);
            Information = 18;
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    case IOCTL_SERIAL_GET_PROPERTIES:
        if (OutputLength >= 64) {
            /* SERIAL_COMMPROP structure (simplified) */
            RtlZeroMemory(OutputBuffer, 64);
            PULONG Props = (PULONG)OutputBuffer;
            Props[0] = 64;                  /* PacketLength */
            Props[1] = 0;                   /* PacketVersion */
            Props[2] = 0xFFFFFFFF;          /* ServiceMask */
            Props[3] = 0;                   /* Reserved1 */
            Props[4] = VSERIAL_BUFFER_SIZE; /* MaxTxQueue */
            Props[5] = VSERIAL_BUFFER_SIZE; /* MaxRxQueue */
            Props[6] = 0x10000001;          /* MaxBaud (BAUD_USER) */
            Props[7] = 0;                   /* ProvSubType */
            Props[8] = 0xFF;                /* ProvCapabilities */
            Props[9] = 0xFF;                /* SettableParams */
            Props[10] = 0x10000001;         /* SettableBaud */
            /* Word 11-12: SettableData/StopParity */
            Props[11] = 0x0F0F;
            Props[12] = VSERIAL_BUFFER_SIZE; /* CurrentTxQueue */
            Props[13] = VSERIAL_BUFFER_SIZE; /* CurrentRxQueue */
            Information = 64;
        } else {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
        
    default:
        DbgPrint("VirtualSerial: Unknown IOCTL 0x%X\n", IoControlCode);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }
    
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return Status;
}
