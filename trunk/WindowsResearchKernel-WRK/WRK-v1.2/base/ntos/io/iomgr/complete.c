/*
Copyright (c) Microsoft Corporation. All rights reserved.

You may only use this code if you agree to the terms of the Windows Research Kernel Source Code License agreement (see License.txt).
If you do not agree to the terms, do not use the code.

Module Name:
    complete.c

Abstract:
   This module implements the executive I/O completion object.
   Functions are provided to create, open, query, and wait for I/O completion objects.
*/

#include "iomgr.h"

// Define forward referenced function prototypes.
VOID IopFreeMiniPacket(PIOP_MINI_COMPLETION_PACKET MiniPacket);

// Define section types for appropriate functions.
#pragma alloc_text(PAGE, NtCreateIoCompletion)
#pragma alloc_text(PAGE, NtOpenIoCompletion)
#pragma alloc_text(PAGE, NtQueryIoCompletion)
#pragma alloc_text(PAGE, NtRemoveIoCompletion)
#pragma alloc_text(PAGE, NtSetIoCompletion)
#pragma alloc_text(PAGE, IoSetIoCompletion)
#pragma alloc_text(PAGE, IopFreeMiniPacket)
#pragma alloc_text(PAGE, IopDeleteIoCompletion)


NTSTATUS NtCreateIoCompletion(__out PHANDLE IoCompletionHandle,
                              __in ACCESS_MASK DesiredAccess,
                              __in_opt POBJECT_ATTRIBUTES ObjectAttributes,
                              __in ULONG Count OPTIONAL
)
/*
Routine Description:
    This function creates an I/O completion object, sets the maximum target concurrent thread count to the specified value,
    and opens a handle to the object with the specified desired access.
Arguments:
    IoCompletionHandle - Supplies a pointer to a variable that receives the I/O completion object handle.
    DesiredAccess - Supplies the desired types of access for the I/O completion object.
    ObjectAttributes - Supplies a pointer to an object attributes structure.
    Count - Supplies the target maximum  number of threads that should be concurrently active.
            If this parameter is not specified, then the number of processors is used.
Return Value:
    STATUS_SUCCESS is returned if the function is success. 
    Otherwise, an error status is returned.
*/
{
    HANDLE Handle;
    KPROCESSOR_MODE PreviousMode;
    PVOID IoCompletion;
    NTSTATUS Status;

    // Establish an exception handler, probe the output handle address, and attempt to create an I/O completion object.
    // If the probe fails, then return the exception code as the service status.
    // Otherwise, return the status value returned by the object insertion routine.
    try {
        PreviousMode = KeGetPreviousMode();// Get previous processor mode and probe output handle address if necessary.
        if (PreviousMode != KernelMode) {
            ProbeForWriteHandle(IoCompletionHandle);
        }

        // Allocate I/O completion object.
        Status = ObCreateObject(PreviousMode, IoCompletionObjectType, ObjectAttributes, PreviousMode, NULL, sizeof(KQUEUE), 0, 0, (PVOID *)&IoCompletion);
        if (NT_SUCCESS(Status)) {// If the I/O completion object was successfully allocated, 
            KeInitializeQueue((PKQUEUE)IoCompletion, Count);//then initialize the object and attempt to insert it in the handle table of the current process.
            Status = ObInsertObject(IoCompletion, NULL, DesiredAccess, 0, (PVOID *)NULL, &Handle);
            if (NT_SUCCESS(Status)) {// If the I/O completion object was successfully inserted in the handle table of the current process, 
                try {
                    *IoCompletionHandle = Handle;//then attempt to write the handle value.
                } except(ExSystemExceptionFilter())
                {// If the write attempt fails, then do not report an error.
                    NOTHING;// When the caller attempts to access the handle value, an access violation will occur.
                }
            }
        }
    } except(ExSystemExceptionFilter())
    {// If an exception occurs during the probe of the output handle address, 
        Status = GetExceptionCode();//then always handle the exception and return the exception code as the status value.
    }

    return Status;// Return service status.
}


NTSTATUS NtOpenIoCompletion(__out PHANDLE IoCompletionHandle, __in ACCESS_MASK DesiredAccess, __in POBJECT_ATTRIBUTES ObjectAttributes)
/*
Routine Description:
    This function opens a handle to an I/O completion object with the specified desired access.
Arguments:
    IoCompletionHandle - Supplies a pointer to a variable that receives the completion object handle.
    DesiredAccess - Supplies the desired types of access for the I/O completion object.
    ObjectAttributes - Supplies a pointer to an object attributes structure.
Return Value:
    STATUS_SUCCESS is returned if the function is success. Otherwise, an error status is returned.
*/
{
    HANDLE Handle;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;

    // Establish an exception handler, probe the output handle address, and attempt to open an I/O completion object.
    // If the probe fails, then return the exception code as the service status.
    // Otherwise, return the status value returned by the object open routine.
    try {
        // Get previous processor mode and probe output handle address if necessary.
        PreviousMode = KeGetPreviousMode();
        if (PreviousMode != KernelMode) {
            ProbeForWriteHandle(IoCompletionHandle);
        }

        // Open handle to the completion object with the specified desired access.
        Status = ObOpenObjectByName(ObjectAttributes, IoCompletionObjectType, PreviousMode, NULL, DesiredAccess, NULL, &Handle);

        // If the open was successful, then attempt to write the I/O completion object handle value.
        // If the write attempt fails, then do not report an error.
        // When the caller attempts to access the handle value, an access violation will occur.
        if (NT_SUCCESS(Status)) {
            try {
                *IoCompletionHandle = Handle;
            } except(ExSystemExceptionFilter())
            {
                NOTHING;
            }
        }
    } except(ExSystemExceptionFilter())
    {// If an exception occurs during the probe of the output handle address,
        Status = GetExceptionCode();// then always handle the exception and return the exception code as the status value.
    }

    return Status;// Return service status.
}


NTSTATUS NtQueryIoCompletion(__in HANDLE IoCompletionHandle,
                             __in IO_COMPLETION_INFORMATION_CLASS IoCompletionInformationClass,
                             __out_bcount(IoCompletionInformationLength) PVOID IoCompletionInformation,
                             __in ULONG IoCompletionInformationLength,
                             __out_opt PULONG ReturnLength
)
/*
Routine Description:
    This function queries the state of an I/O completion object and returns the requested information in the specified record structure.
Arguments:
    IoCompletionHandle - Supplies a handle to an I/O completion object.
    IoCompletionInformationClass - Supplies the class of information being requested.
    IoCompletionInformation - Supplies a pointer to a record that receives the requested information.
    IoCompletionInformationLength - Supplies the length of the record that receives the requested information.
    ReturnLength - Supplies an optional pointer to a variable that receives the actual length of the information that is returned.
Return Value:
    STATUS_SUCCESS is returned if the function is success. Otherwise, an error status is returned.
*/
{
    PVOID IoCompletion;
    LONG Depth;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;

    // Establish an exception handler, probe the output arguments, reference the I/O completion object, and return the specified information.
    // If the probe fails, then return the exception code as the service status.
    // Otherwise return the status value returned by the reference object by handle routine.
    try {
        // Get previous processor mode and probe output arguments if necessary.
        PreviousMode = KeGetPreviousMode();
        if (PreviousMode != KernelMode) {
            C_ASSERT(sizeof(IO_COMPLETION_BASIC_INFORMATION) == sizeof(ULONG));
            ProbeForWriteUlongAligned32((PULONG)IoCompletionInformation);
            if (ARGUMENT_PRESENT(ReturnLength)) {
                ProbeForWriteUlong(ReturnLength);
            }
        }

        // Check argument validity.
        if (IoCompletionInformationClass != IoCompletionBasicInformation) {
            return STATUS_INVALID_INFO_CLASS;
        }
        if (IoCompletionInformationLength != sizeof(IO_COMPLETION_BASIC_INFORMATION)) {
            return STATUS_INFO_LENGTH_MISMATCH;
        }

        // Reference the I/O completion object by handle.
        Status = ObReferenceObjectByHandle(IoCompletionHandle, IO_COMPLETION_QUERY_STATE, IoCompletionObjectType, PreviousMode, &IoCompletion, NULL);
        if (NT_SUCCESS(Status)) {// If the reference was successful, 
            Depth = KeReadStateQueue((PKQUEUE)IoCompletion);//then read the current state of the I/O completion object, 
            ObDereferenceObject(IoCompletion);//// dereference the I/O completion object,   
            try {
                ((PIO_COMPLETION_BASIC_INFORMATION)IoCompletionInformation)->Depth = Depth;//fill in the information structure,   
                if (ARGUMENT_PRESENT(ReturnLength)) {//and return the structure length if specified.   
                    *ReturnLength = sizeof(IO_COMPLETION_BASIC_INFORMATION);//
                }
            } except(ExSystemExceptionFilter())
            {//If the write of the I/O completion information or the return length fails, then do not report an error.
                NOTHING;// When the caller accesses the information structure or length an access violation will occur.
            }
        }
    } except(ExSystemExceptionFilter())
    {// If an exception occurs during the probe of the output arguments,
        Status = GetExceptionCode();// then always handle the exception and return the exception code as the status value.
    }

    return Status;// Return service status.
}


NTSTATUS NtSetIoCompletion(__in HANDLE IoCompletionHandle, 
                           __in PVOID KeyContext, 
                           __in_opt PVOID ApcContext,
                           __in NTSTATUS IoStatus, 
                           __in ULONG_PTR IoStatusInformation
)
/*
Routine Description:
    This function allows the caller to queue an Irp to an I/O completion port and 
    specify all of the information that is returned out the other end using NtRemoveIoCompletion.
Arguments:
    IoCompletionHandle - Supplies a handle to the io completion port that the caller intends to queue a completion packet to
    KeyContext - Supplies the key context that is returned during a call to NtRemoveIoCompletion
    ApcContext - Supplies the apc context that is returned during a call to NtRemoveIoCompletion
    IoStatus - Supplies the IoStatus->Status data that is returned during a call to NtRemoveIoCompletion
    IoStatusInformation - Supplies the IoStatus->Information data that is returned during a call to NtRemoveIoCompletion
Return Value:
    STATUS_SUCCESS is returned if the function is success. Otherwise, an error status is returned.
*/
{
    PVOID IoCompletion;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(IoCompletionHandle, IO_COMPLETION_MODIFY_STATE, IoCompletionObjectType, KeGetPreviousMode(), &IoCompletion, NULL);
    if (NT_SUCCESS(Status)) {
        Status = IoSetIoCompletion(IoCompletion, KeyContext, ApcContext, IoStatus, IoStatusInformation, TRUE);
        ObDereferenceObject(IoCompletion);
    }

    return Status;
}


NTSTATUS NtRemoveIoCompletion(__in HANDLE IoCompletionHandle,
                              __out PVOID* KeyContext,
                              __out PVOID* ApcContext,
                              __out PIO_STATUS_BLOCK IoStatusBlock,
                              __in_opt PLARGE_INTEGER Timeout
)
    /*
    Routine Description:
        This function removes an entry from an I/O completion object.
        If there are currently no entries available, then the calling thread waits for an entry.
    Arguments:
        Completion - Supplies a handle to an I/O completion object.
        KeyContext - Supplies a pointer to a variable that receives the key context that was specified when the I/O completion object was associated with a file object.
        ApcContext - Supplies a pointer to a variable that receives the context that was specified when the I/O operation was issued.
        IoStatus - Supplies a pointer to a variable that receives the I/O completion status.
        Timeout - Supplies a pointer to an optional time out value.
    Return Value:
        STATUS_SUCCESS is returned if the function is success. Otherwise, an error status is returned.
    */
{
    PLARGE_INTEGER CapturedTimeout;
    PLIST_ENTRY Entry;
    PVOID IoCompletion;
    PIRP Irp;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
    LARGE_INTEGER TimeoutValue;
    PVOID LocalApcContext;
    PVOID LocalKeyContext;
    IO_STATUS_BLOCK LocalIoStatusBlock;
    PIOP_MINI_COMPLETION_PACKET MiniPacket;

    // Establish an exception handler, probe the I/O context, the I/O status, and the optional timeout value if specified,
    // reference the I/O completion object, and attempt to remove an entry from the I/O completion object.
    // If the probe fails, then return the exception code as the service status.
    // Otherwise, return a value dependent on the outcome of the queue removal.
    try {
        // Get previous processor mode and probe the I/O context, status, and timeout if necessary.
        CapturedTimeout = NULL;
        PreviousMode = KeGetPreviousMode();
        if (PreviousMode != KernelMode) {
            ProbeForWriteUlong_ptr((PULONG_PTR)ApcContext);
            ProbeForWriteUlong_ptr((PULONG_PTR)KeyContext);
            ProbeForWriteIoStatus(IoStatusBlock);
            if (ARGUMENT_PRESENT(Timeout)) {
                CapturedTimeout = &TimeoutValue;
                TimeoutValue = ProbeAndReadLargeInteger(Timeout);
            }
        } else {
            if (ARGUMENT_PRESENT(Timeout)) {
                CapturedTimeout = Timeout;
            }
        }

        // Reference the I/O completion object by handle.
        Status = ObReferenceObjectByHandle(IoCompletionHandle, IO_COMPLETION_MODIFY_STATE, IoCompletionObjectType, PreviousMode, &IoCompletion, NULL);

        // If an entry is removed from the I/O completion object, then capture the completion information, 
        // release the associated IRP, and attempt to write the completion information.        
        if (NT_SUCCESS(Status)) {// If the reference was successful, 
            Entry = KeRemoveQueue((PKQUEUE)IoCompletion, PreviousMode, CapturedTimeout);//then attempt to remove an entry from the I/O completion object.

            // N.B. The entry value returned can be the address of a list entry, STATUS_USER_APC, or STATUS_TIMEOUT.
            if (((LONG_PTR)Entry == STATUS_TIMEOUT) || ((LONG_PTR)Entry == STATUS_USER_APC)) {
                Status = (NTSTATUS)((LONG_PTR)Entry);
            } else {
                Status = STATUS_SUCCESS;// Set the completion status, capture the completion information, 
                try {
                    MiniPacket = CONTAINING_RECORD(Entry, IOP_MINI_COMPLETION_PACKET, ListEntry);
                    if (MiniPacket->PacketType == IopCompletionPacketIrp) {
                        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
                        LocalApcContext = Irp->Overlay.AsynchronousParameters.UserApcContext;
                        LocalKeyContext = (PVOID)Irp->Tail.CompletionKey;
                        LocalIoStatusBlock = Irp->IoStatus;
                        IoFreeIrp(Irp);//deallocate the associated IRP, 
                    } else {
                        LocalApcContext = MiniPacket->ApcContext;
                        LocalKeyContext = (PVOID)MiniPacket->KeyContext;
                        LocalIoStatusBlock.Status = MiniPacket->IoStatus;
                        LocalIoStatusBlock.Information = MiniPacket->IoStatusInformation;
                        IopFreeMiniPacket(MiniPacket);
                    }

                    *ApcContext = LocalApcContext;//and attempt to write the completion information.
                    *KeyContext = LocalKeyContext;
                    *IoStatusBlock = LocalIoStatusBlock;
                } except(ExSystemExceptionFilter())
                {// If the write of the completion information fails, then do not report an error.
                    NOTHING;// When the caller attempts to access the completion information, an access violation will occur.
                }
            }

            ObDereferenceObject(IoCompletion);// Deference I/O completion object.
        }
    } except(ExSystemExceptionFilter())
    {// If an exception occurs during the probe of the previous count,
        Status = GetExceptionCode();// then always handle the exception and return the exception code as the status value.
    }

    return Status;// Return service status.
}


NTKERNELAPI NTSTATUS IoSetIoCompletion(IN PVOID IoCompletion,
                                       IN PVOID KeyContext, 
                                       IN PVOID ApcContext,
                                       IN NTSTATUS IoStatus, 
                                       IN ULONG_PTR IoStatusInformation,
                                       IN BOOLEAN Quota
)
/*
Routine Description:
    This function allows the caller to queue an Irp to an I/O completion port and 
    specify all of the information that is returned out the other end using NtRemoveIoCompletion.
Arguments:
    IoCompletion - Supplies a a pointer to the completion port that the caller intends to queue a completion packet to.
    KeyContext - Supplies the key context that is returned during a call to NtRemoveIoCompletion.
    ApcContext - Supplies the apc context that is returned during a call to NtRemoveIoCompletion.
    IoStatus - Supplies the IoStatus->Status data that is returned during a call to NtRemoveIoCompletion.
    IoStatusInformation - Supplies the IoStatus->Information data that is returned during a call to NtRemoveIoCompletion.
Return Value:
    STATUS_SUCCESS is returned if the function is success. Otherwise, an error status is returned.
*/
{
    PGENERAL_LOOKASIDE Lookaside;
    PIOP_MINI_COMPLETION_PACKET MiniPacket;
    ULONG PacketType;
    PKPRCB Prcb;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    // Attempt to allocate the minpacket from the per processor lookaside list.
    PacketType = IopCompletionPacketMini;
    Prcb = KeGetCurrentPrcb();
    Lookaside = Prcb->PPLookasideList[LookasideCompletionList].P;
    Lookaside->TotalAllocates += 1;
    MiniPacket = (PVOID)InterlockedPopEntrySList(&Lookaside->ListHead);

    // If the per processor lookaside list allocation failed, then attempt to allocate from the system lookaside list.
    if (MiniPacket == NULL) {
        Lookaside->AllocateMisses += 1;
        Lookaside = Prcb->PPLookasideList[LookasideCompletionList].L;
        Lookaside->TotalAllocates += 1;
        MiniPacket = (PVOID)InterlockedPopEntrySList(&Lookaside->ListHead);
    }

    if (MiniPacket == NULL) {// If both lookaside allocation attempts failed, then attempt to allocate from pool.
        Lookaside->AllocateMisses += 1;

        if (Quota != FALSE) {// If quota is specified, then allocate pool with quota charged.
            PacketType = IopCompletionPacketQuota;
            try {
                MiniPacket = ExAllocatePoolWithQuotaTag(NonPagedPool, sizeof(*MiniPacket), ' pcI');
            } except(EXCEPTION_EXECUTE_HANDLER)
            {
                NOTHING;
            }
        } else {// Otherwise, allocate pool without quota.
            MiniPacket = ExAllocatePoolWithTagPriority(NonPagedPool, sizeof(*MiniPacket), ' pcI', LowPoolPriority);
        }
    }

    // If a minipacket was successfully allocated, then initialize and queue the packet to the specified I/O completion queue.
    if (MiniPacket != NULL) {
        MiniPacket->PacketType = PacketType;
        MiniPacket->KeyContext = KeyContext;
        MiniPacket->ApcContext = ApcContext;
        MiniPacket->IoStatus = IoStatus;
        MiniPacket->IoStatusInformation = IoStatusInformation;
        KeInsertQueue((PKQUEUE)IoCompletion, &MiniPacket->ListEntry);
    } else {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}


VOID IopFreeMiniPacket(PIOP_MINI_COMPLETION_PACKET MiniPacket)
/*
Routine Description:
    This function free the specified I/O completion packet.
Arguments:
    MiniPacket - Supplies a pointer to an I/O completion minipacket.
*/
{
    PGENERAL_LOOKASIDE Lookaside;
    PKPRCB Prcb;

    // If the minipacket cannot be returned to either the per processor or system lookaside list, then free the minipacket to pool.
    // Otherwise, release the quota if quota was allocated and push the entry onto one of the lookaside lists.
    Prcb = KeGetCurrentPrcb();
    Lookaside = Prcb->PPLookasideList[LookasideCompletionList].P;
    Lookaside->TotalFrees += 1;
    if (ExQueryDepthSList(&Lookaside->ListHead) >= Lookaside->Depth) {
        Lookaside->FreeMisses += 1;
        Lookaside = Prcb->PPLookasideList[LookasideCompletionList].L;
        Lookaside->TotalFrees += 1;
        if (ExQueryDepthSList(&Lookaside->ListHead) >= Lookaside->Depth) {
            Lookaside->FreeMisses += 1;
            ExFreePool(MiniPacket);
        } else {
            if (MiniPacket->PacketType == IopCompletionPacketQuota) {
                ExReturnPoolQuota(MiniPacket);
            }

            InterlockedPushEntrySList(&Lookaside->ListHead, (PSLIST_ENTRY)MiniPacket);
        }
    } else {
        if (MiniPacket->PacketType == IopCompletionPacketQuota) {
            ExReturnPoolQuota(MiniPacket);
        }

        InterlockedPushEntrySList(&Lookaside->ListHead, (PSLIST_ENTRY)MiniPacket);
    }
}


VOID IopDeleteIoCompletion(IN PVOID Object)
/*
Routine Description:
    This function is the delete routine for I/O completion objects.
    Its function is to release all the entries in the repsective completion queue and to rundown all threads that are current associated.
Arguments:
    Object - Supplies a pointer to an executive I/O completion object.
*/
{
    PLIST_ENTRY FirstEntry;
    PIRP Irp;
    PLIST_ENTRY NextEntry;
    PIOP_MINI_COMPLETION_PACKET MiniPacket;

    // Rundown threads associated with the I/O completion object and get the list of unprocessed I/O completion IRPs.
    FirstEntry = KeRundownQueue((PKQUEUE)Object);
    if (FirstEntry != NULL) {
        NextEntry = FirstEntry;
        do {
            MiniPacket = CONTAINING_RECORD(NextEntry, IOP_MINI_COMPLETION_PACKET, ListEntry);
            NextEntry = NextEntry->Flink;
            if (MiniPacket->PacketType == IopCompletionPacketIrp) {
                Irp = CONTAINING_RECORD(MiniPacket, IRP, Tail.Overlay.ListEntry);
                IoFreeIrp(Irp);
            } else {
                IopFreeMiniPacket(MiniPacket);
            }
        } while (FirstEntry != NextEntry);
    }
}
