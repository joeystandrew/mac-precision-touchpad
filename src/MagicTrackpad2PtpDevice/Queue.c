// Queue.c: This file contains the queue entry points and callbacks.

#include "driver.h"
#include "queue.tmh"

NTSTATUS
MagicTrackpad2PtpDeviceQueueInitialize(
	_In_ WDFDEVICE Device
)
/*++

Routine Description:


	 The I/O dispatch callbacks for the frameworks device object
	 are configured in this function.

	 A single default I/O Queue is configured for parallel request
	 processing, and a driver context memory allocation is created
	 to hold our structure QUEUE_CONTEXT.

Arguments:

	Device - Handle to a framework device object.

Return Value:

	VOID

--*/
{
	WDFQUEUE queue;
	NTSTATUS status;
	WDF_IO_QUEUE_CONFIG    queueConfig;
	PDEVICE_CONTEXT	       deviceContext;

	deviceContext = DeviceGetContext(Device);

	//
	// Configure a default queue so that requests that are not
	// configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
	// other queues get dispatched here.
	//
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
		&queueConfig,
		WdfIoQueueDispatchParallel
	);

	queueConfig.EvtIoDeviceControl = MagicTrackpad2PtpDeviceEvtIoDeviceControl;
	queueConfig.EvtIoStop = MagicTrackpad2PtpDeviceEvtIoStop;

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate (Primary) failed %!STATUS!", status);
		return status;
	}

	//
	// Create secondary queues for touch and mouse read requests.
	// 

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&deviceContext->MouseQueue);

	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate (Mouse) failed %!STATUS!", status);
		return status;
	}

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&deviceContext->TouchQueue);

	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate (Touch) failed %!STATUS!", status);
		return status;
	}

	return status;
}

VOID
MagicTrackpad2PtpDeviceEvtIoDeviceControl(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength,
	_In_ ULONG IoControlCode
)
{
	
	NTSTATUS status = STATUS_SUCCESS;
	WDFDEVICE device = WdfIoQueueGetDevice(Queue);
	BOOLEAN requestPending = FALSE;

	TraceEvents(TRACE_LEVEL_INFORMATION,
		TRACE_QUEUE,
		"%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d",
		Queue, Request, (int)OutputBufferLength, (int)InputBufferLength, IoControlCode);

	switch (IoControlCode)
	{
		case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
			status = MagicTrackpad2GetHidDescriptor(device, Request);
			break;
		case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
			status = MagicTrackpad2GetDeviceAttribs(device, Request);
			break;
		case IOCTL_HID_GET_REPORT_DESCRIPTOR:
			status = MagicTrackpad2GetReportDescriptor(device, Request);
			break;
		case IOCTL_HID_GET_STRING:
			status = MagicTrackpad2GetStrings(device, Request);
			break;
		case IOCTL_HID_READ_REPORT:
			status = AmtPtpDispatchReadReportRequests(device, Request, &requestPending);
			break;
		case IOCTL_HID_WRITE_REPORT:
		case IOCTL_UMDF_HID_GET_INPUT_REPORT:
		case IOCTL_UMDF_HID_SET_OUTPUT_REPORT:
		case IOCTL_UMDF_HID_GET_FEATURE:
		case IOCTL_UMDF_HID_SET_FEATURE:
		case IOCTL_HID_ACTIVATE_DEVICE:
		case IOCTL_HID_DEACTIVATE_DEVICE:
		case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		default:
			status = STATUS_NOT_SUPPORTED;
			break;
	}

	if (requestPending != TRUE)
	{
		WdfRequestComplete(Request, status);
	}

	return;
}

VOID
MagicTrackpad2PtpDeviceEvtIoStop(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ ULONG ActionFlags
)
/*++

Routine Description:

	This event is invoked for a power-managed queue before the device leaves the working state (D0).

Arguments:

	Queue -  Handle to the framework queue object that is associated with the
			 I/O request.

	Request - Handle to a framework request object.

	ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS-typed flags
				  that identify the reason that the callback function is being called
				  and whether the request is cancelable.

Return Value:

	VOID

--*/
{
	TraceEvents(TRACE_LEVEL_INFORMATION,
		TRACE_QUEUE,
		"%!FUNC! Queue 0x%p, Request 0x%p ActionFlags %d",
		Queue, Request, ActionFlags);

	//
	// In most cases, the EvtIoStop callback function completes, cancels, or postpones
	// further processing of the I/O request.
	//
	// Typically, the driver uses the following rules:
	//
	// - If the driver owns the I/O request, it either postpones further processing
	//   of the request and calls WdfRequestStopAcknowledge, or it calls WdfRequestComplete
	//   with a completion status value of STATUS_SUCCESS or STATUS_CANCELLED.
	//  
	//   The driver must call WdfRequestComplete only once, to either complete or cancel
	//   the request. To ensure that another thread does not call WdfRequestComplete
	//   for the same request, the EvtIoStop callback must synchronize with the driver's
	//   other event callback functions, for instance by using interlocked operations.
	//
	// - If the driver has forwarded the I/O request to an I/O target, it either calls
	//   WdfRequestCancelSentRequest to attempt to cancel the request, or it postpones
	//   further processing of the request and calls WdfRequestStopAcknowledge.
	//
	// A driver might choose to take no action in EvtIoStop for requests that are
	// guaranteed to complete in a small amount of time. For example, the driver might
	// take no action for requests that are completed in one of the driver�s request handlers.
	//

	return;
}

NTSTATUS
AmtPtpDispatchReadReportRequests(
	_In_ WDFDEVICE Device,
	_In_ WDFREQUEST Request,
	_Out_ BOOLEAN *Pending
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT devContext;

	status = STATUS_SUCCESS;
	devContext = DeviceGetContext(Device);

	status = WdfRequestForwardToIoQueue(Request,
		(devContext->IsWellspringModeOn)? devContext->TouchQueue : devContext->MouseQueue);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "%!FUNC!: WdfRequestForwardToIoQueue failed with %!STATUS!", status);
		return status;
	}

	if (NULL != Pending)
	{
		*Pending = TRUE;
	}

	return status;
}