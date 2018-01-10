/** @file
	NvmExpressDxe driver is used to manage non-volatile memory subsystem which follows
	NVM Express specification.

	Copyright (c) 2013 - 2016, Intel Corporation. All rights reserved.<BR>
	This program and the accompanying materials
	are licensed and made available under the terms and conditions of the BSD License
	which accompanies this distribution.  The full text of the license may be found at
	http://opensource.org/licenses/bsd-license.php.

	THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
	WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#include <efi.h>
#include <efilib.h>

#include "NvmExpress.h"

/**
	Read some sectors from the device.

	@param  Device                 The pointer to the NVME_DEVICE_PRIVATE_DATA data structure.
	@param  Buffer                 The buffer used to store the data read from the device.
	@param  Lba                    The start block number.
	@param  Blocks                 Total block number to be read.

	@retval EFI_SUCCESS            Datum are read from the device.
	@retval Others                 Fail to read all the datum.
**/
EFI_STATUS
ReadSectors (
	IN NVME_DEVICE_PRIVATE_DATA  *Device,
	IN UINT64                    Buffer,
	IN UINT64                    Lba,
	IN UINT32                    Blocks
)
{
	NVME_CONTROLLER_PRIVATE_DATA             *Private;
	UINT32                                   Bytes;
	EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET CommandPacket;
	EFI_NVM_EXPRESS_COMMAND                  Command;
	EFI_NVM_EXPRESS_COMPLETION               Completion;
	EFI_STATUS                               Status;
	UINT32                                   BlockSize;

	Private    = Device->Controller;
	BlockSize  = Device->Media.BlockSize;
	Bytes      = Blocks * BlockSize;

	ZeroMem (&CommandPacket, sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET));
	ZeroMem (&Command, sizeof(EFI_NVM_EXPRESS_COMMAND));
	ZeroMem (&Completion, sizeof(EFI_NVM_EXPRESS_COMPLETION));

	CommandPacket.NvmeCmd        = &Command;
	CommandPacket.NvmeCompletion = &Completion;

	CommandPacket.NvmeCmd->Cdw0.Opcode = NVME_IO_READ_OPC;
	CommandPacket.NvmeCmd->Nsid        = Device->NamespaceId;
	CommandPacket.TransferBuffer       = (VOID *)(UINTN)Buffer;

	CommandPacket.TransferLength = Bytes;
	CommandPacket.CommandTimeout = NVME_GENERIC_TIMEOUT;
	CommandPacket.QueueType      = NVME_IO_QUEUE;

	CommandPacket.NvmeCmd->Cdw10 = (UINT32)Lba;
	CommandPacket.NvmeCmd->Cdw11 = (UINT32)RShiftU64(Lba, 32);
	CommandPacket.NvmeCmd->Cdw12 = (Blocks - 1) & 0xFFFF;

	CommandPacket.NvmeCmd->Flags = CDW10_VALID | CDW11_VALID | CDW12_VALID;

	Status = Private->Passthru.PassThru (
		&Private->Passthru,
		Device->NamespaceId,
		&CommandPacket,
		NULL
		);

  return Status;
}

/**
	Write some sectors to the device.

	@param  Device                 The pointer to the NVME_DEVICE_PRIVATE_DATA data structure.
	@param  Buffer                 The buffer to be written into the device.
	@param  Lba                    The start block number.
	@param  Blocks                 Total block number to be written.

	@retval EFI_SUCCESS            Datum are written into the buffer.
	@retval Others                 Fail to write all the datum.
**/
EFI_STATUS
WriteSectors (
	IN NVME_DEVICE_PRIVATE_DATA      *Device,
	IN UINT64                        Buffer,
	IN UINT64                        Lba,
	IN UINT32                        Blocks
)
{
	NVME_CONTROLLER_PRIVATE_DATA             *Private;
	EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET CommandPacket;
	EFI_NVM_EXPRESS_COMMAND                  Command;
	EFI_NVM_EXPRESS_COMPLETION               Completion;
	EFI_STATUS                               Status;
	UINT32                                   Bytes;
	UINT32                                   BlockSize;

	Private    = Device->Controller;
	BlockSize  = Device->Media.BlockSize;
	Bytes      = Blocks * BlockSize;

	ZeroMem (&CommandPacket, sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET));
	ZeroMem (&Command, sizeof(EFI_NVM_EXPRESS_COMMAND));
	ZeroMem (&Completion, sizeof(EFI_NVM_EXPRESS_COMPLETION));

	CommandPacket.NvmeCmd        = &Command;
	CommandPacket.NvmeCompletion = &Completion;

	CommandPacket.NvmeCmd->Cdw0.Opcode = NVME_IO_WRITE_OPC;
	CommandPacket.NvmeCmd->Nsid  = Device->NamespaceId;
	CommandPacket.TransferBuffer = (VOID *)(UINTN)Buffer;

	CommandPacket.TransferLength = Bytes;
	CommandPacket.CommandTimeout = NVME_GENERIC_TIMEOUT;
	CommandPacket.QueueType      = NVME_IO_QUEUE;

	CommandPacket.NvmeCmd->Cdw10 = (UINT32)Lba;
	CommandPacket.NvmeCmd->Cdw11 = (UINT32)RShiftU64(Lba, 32);
	//
	// Set Force Unit Access bit (bit 30) to use write-through behaviour
	//
	CommandPacket.NvmeCmd->Cdw12 = ((Blocks - 1) & 0xFFFF) | BIT30;

	CommandPacket.MetadataBuffer = NULL;
	CommandPacket.MetadataLength = 0;

	CommandPacket.NvmeCmd->Flags = CDW10_VALID | CDW11_VALID | CDW12_VALID;

	Status = Private->Passthru.PassThru (
		&Private->Passthru,
		Device->NamespaceId,
		&CommandPacket,
		NULL
		);

	return Status;
}

/**
	Read some blocks from the device.

	@param  Device                 The pointer to the NVME_DEVICE_PRIVATE_DATA data structure.
	@param  Buffer                 The buffer used to store the data read from the device.
	@param  Lba                    The start block number.
	@param  Blocks                 Total block number to be read.

	@retval EFI_SUCCESS            Datum are read from the device.
	@retval Others                 Fail to read all the datum.
**/
EFI_STATUS
NvmeRead (
	IN  NVME_DEVICE_PRIVATE_DATA  *Device,
	OUT VOID   *Buffer,
	IN  UINT64 Lba,
	IN  UINTN  Blocks
)
{
	EFI_STATUS                       Status;
	UINT32                           BlockSize;
	NVME_CONTROLLER_PRIVATE_DATA     *Private;
	UINT32                           MaxTransferBlocks;
	BOOLEAN                          IsEmpty;

	//
	// Wait for the device's asynchronous I/O queue to become empty.
	//
	while (TRUE) {
		IsEmpty = IsListEmpty (&Device->AsyncQueue);

		if (IsEmpty)
			break;

		NanoSecondDelay(100 * 1000);
	}

	Status        = EFI_SUCCESS;
	Private       = Device->Controller;
	BlockSize     = Device->Media.BlockSize;

	if (Private->ControllerData->Mdts != 0) {
		MaxTransferBlocks = (1 << (Private->ControllerData->Mdts)) * (1 << (Private->Cap.Mpsmin + 12)) / BlockSize;
	} else {
		MaxTransferBlocks = 1024;
	}

	while (Blocks > 0) {
		if (Blocks >= MaxTransferBlocks) {
			Status = ReadSectors (Device, (UINT64)(UINTN)Buffer, Lba, MaxTransferBlocks);
			Blocks -= MaxTransferBlocks;
			Buffer  = (VOID *)(UINTN)((UINT64)(UINTN)Buffer + MaxTransferBlocks * BlockSize);
			Lba    += MaxTransferBlocks;
		} else {
			Status = ReadSectors (Device, (UINT64)(UINTN)Buffer, Lba, (UINT32)Blocks);
			Blocks = 0;
		}

		if (EFI_ERROR(Status))
			break;
	}

	return Status;
}

/**
	Write some blocks to the device.

	@param  Device                 The pointer to the NVME_DEVICE_PRIVATE_DATA data structure.
	@param  Buffer                 The buffer to be written into the device.
	@param  Lba                    The start block number.
	@param  Blocks                 Total block number to be written.

	@retval EFI_SUCCESS            Datum are written into the buffer.
	@retval Others                 Fail to write all the datum.
**/
EFI_STATUS
NvmeWrite (
	IN NVME_DEVICE_PRIVATE_DATA *Device,
	IN VOID                     *Buffer,
	IN UINT64                   Lba,
	IN UINTN                    Blocks
)
{
	EFI_STATUS                       Status;
	UINT32                           BlockSize;
	NVME_CONTROLLER_PRIVATE_DATA     *Private;
	UINT32                           MaxTransferBlocks;
	BOOLEAN                          IsEmpty;

	//
	// Wait for the device's asynchronous I/O queue to become empty.
	//
	while (TRUE) {
		IsEmpty = IsListEmpty (&Device->AsyncQueue);

		if (IsEmpty) {
			break;
		}

		NanoSecondDelay(100 * 1000);
	}

	Status        = EFI_SUCCESS;
	Private       = Device->Controller;
	BlockSize     = Device->Media.BlockSize;

	if (Private->ControllerData->Mdts != 0) {
		MaxTransferBlocks = (1 << (Private->ControllerData->Mdts)) * (1 << (Private->Cap.Mpsmin + 12)) / BlockSize;
	} else {
		MaxTransferBlocks = 1024;
	}

	while (Blocks > 0) {
		if (Blocks >= MaxTransferBlocks) {
			Status = WriteSectors (Device, (UINT64)(UINTN)Buffer, Lba, MaxTransferBlocks);

			Blocks -= MaxTransferBlocks;
			Buffer  = (VOID *)(UINTN)((UINT64)(UINTN)Buffer + MaxTransferBlocks * BlockSize);
			Lba    += MaxTransferBlocks;
		} else {
			Status = WriteSectors (Device, (UINT64)(UINTN)Buffer, Lba, (UINT32)Blocks);
			Blocks = 0;
		}

		if (EFI_ERROR(Status)) {
			break;
		}
	}

	return Status;
}

/**
	Flushes all modified data to the device.

	@param  Device                 The pointer to the NVME_DEVICE_PRIVATE_DATA data structure.

	@retval EFI_SUCCESS            Datum are written into the buffer.
	@retval Others                 Fail to write all the datum.
**/
EFI_STATUS
NvmeFlush (
	IN NVME_DEVICE_PRIVATE_DATA      *Device
)
{
	NVME_CONTROLLER_PRIVATE_DATA             *Private;
	EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET CommandPacket;
	EFI_NVM_EXPRESS_COMMAND                  Command;
	EFI_NVM_EXPRESS_COMPLETION               Completion;
	EFI_STATUS                               Status;

	Private = Device->Controller;

	ZeroMem (&CommandPacket, sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET));
	ZeroMem (&Command, sizeof(EFI_NVM_EXPRESS_COMMAND));
	ZeroMem (&Completion, sizeof(EFI_NVM_EXPRESS_COMPLETION));

	CommandPacket.NvmeCmd        = &Command;
	CommandPacket.NvmeCompletion = &Completion;

	CommandPacket.NvmeCmd->Cdw0.Opcode = NVME_IO_FLUSH_OPC;
	CommandPacket.NvmeCmd->Nsid  = Device->NamespaceId;
	CommandPacket.CommandTimeout = NVME_GENERIC_TIMEOUT;
	CommandPacket.QueueType      = NVME_IO_QUEUE;

	Status = Private->Passthru.PassThru (
		&Private->Passthru,
		Device->NamespaceId,
		&CommandPacket,
		NULL
		);

	return Status;
}

/**
	Reset the Block Device.

	@param  This                 Indicates a pointer to the calling context.
	@param  ExtendedVerification Driver may perform diagnostics on reset.

	@retval EFI_SUCCESS          The device was reset.
	@retval EFI_DEVICE_ERROR     The device is not functioning properly and could
	                             not be reset.
**/
EFI_STATUS
EFIAPI
NvmeBlockIoReset (
	IN  EFI_BLOCK_IO_PROTOCOL   *This,
	IN  BOOLEAN                 ExtendedVerification
)
{
	NVME_CONTROLLER_PRIVATE_DATA    *Private;
	NVME_DEVICE_PRIVATE_DATA        *Device;
	EFI_STATUS                      Status;

	ExtendedVerification = ExtendedVerification;

	if (This == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	//
	// For Nvm Express subsystem, reset block device means reset controller.
	//

	Device  = NVME_DEVICE_PRIVATE_DATA_FROM_BLOCK_IO (This);

	Private = Device->Controller;

	Status  = NvmeControllerInit (Private);

	if (EFI_ERROR (Status)) {
		Status = EFI_DEVICE_ERROR;
	}

	return Status;
}

/**
  Read BufferSize bytes from Lba into Buffer.

  @param  This       Indicates a pointer to the calling context.
  @param  MediaId    Id of the media, changes every time the media is replaced.
  @param  Lba        The starting Logical Block Address to read from.
  @param  BufferSize Size of Buffer, must be a multiple of device block size.
  @param  Buffer     A pointer to the destination buffer for the data. The caller is
                     responsible for either having implicit or explicit ownership of the buffer.

  @retval EFI_SUCCESS           The data was read correctly from the device.
  @retval EFI_DEVICE_ERROR      The device reported an error while performing the read.
  @retval EFI_NO_MEDIA          There is no media in the device.
  @retval EFI_MEDIA_CHANGED     The MediaId does not matched the current device.
  @retval EFI_BAD_BUFFER_SIZE   The Buffer was not a multiple of the block size of the device.
  @retval EFI_INVALID_PARAMETER The read request contains LBAs that are not valid,
                                or the buffer is not on proper alignment.
**/
EFI_STATUS
EFIAPI
NvmeBlockIoReadBlocks (
	IN  EFI_BLOCK_IO_PROTOCOL   *This,
	IN  UINT32                  MediaId,
	IN  EFI_LBA                 Lba,
	IN  UINTN                   BufferSize,
	OUT VOID                    *Buffer
)
{
	NVME_DEVICE_PRIVATE_DATA    *Device;
	EFI_STATUS                  Status;
	EFI_BLOCK_IO_MEDIA          *Media;
	UINTN                       BlockSize;
	UINTN                       NumberOfBlocks;
	UINTN                       IoAlign;

	//
	// Check parameters.
	//
	if (This == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	Media = This->Media;
	if (MediaId != Media->MediaId)
		return EFI_MEDIA_CHANGED;

	if (Buffer == NULL)
		return EFI_INVALID_PARAMETER;

	if (BufferSize == 0)
		return EFI_SUCCESS;

	BlockSize = Media->BlockSize;
	if ((BufferSize % BlockSize) != 0)
		return EFI_BAD_BUFFER_SIZE;

	NumberOfBlocks  = BufferSize / BlockSize;
	if ((Lba + NumberOfBlocks - 1) > Media->LastBlock)
		return EFI_INVALID_PARAMETER;

	IoAlign = Media->IoAlign;
	if (IoAlign > 0 && (((UINTN) Buffer & (IoAlign - 1)) != 0))
		return EFI_INVALID_PARAMETER;

	Device = NVME_DEVICE_PRIVATE_DATA_FROM_BLOCK_IO (This);
	Status = NvmeRead (Device, Buffer, Lba, NumberOfBlocks);

	return Status;
}

/**
  Write BufferSize bytes from Lba into Buffer.

  @param  This       Indicates a pointer to the calling context.
  @param  MediaId    The media ID that the write request is for.
  @param  Lba        The starting logical block address to be written. The caller is
                     responsible for writing to only legitimate locations.
  @param  BufferSize Size of Buffer, must be a multiple of device block size.
  @param  Buffer     A pointer to the source buffer for the data.

  @retval EFI_SUCCESS           The data was written correctly to the device.
  @retval EFI_WRITE_PROTECTED   The device can not be written to.
  @retval EFI_DEVICE_ERROR      The device reported an error while performing the write.
  @retval EFI_NO_MEDIA          There is no media in the device.
  @retval EFI_MEDIA_CHNAGED     The MediaId does not matched the current device.
  @retval EFI_BAD_BUFFER_SIZE   The Buffer was not a multiple of the block size of the device.
  @retval EFI_INVALID_PARAMETER The write request contains LBAs that are not valid,
                                or the buffer is not on proper alignment.
**/
EFI_STATUS
EFIAPI
NvmeBlockIoWriteBlocks (
	IN  EFI_BLOCK_IO_PROTOCOL   *This,
	IN  UINT32                  MediaId,
	IN  EFI_LBA                 Lba,
	IN  UINTN                   BufferSize,
	IN  VOID                    *Buffer
)
{
	NVME_DEVICE_PRIVATE_DATA  *Device;
	EFI_STATUS                Status;
	EFI_BLOCK_IO_MEDIA        *Media;
	UINTN                     BlockSize;
	UINTN                     NumberOfBlocks;
	UINTN                     IoAlign;

	//
	// Check parameters.
	//
	if (This == NULL)
		return EFI_INVALID_PARAMETER;

	Media = This->Media;

	if (MediaId != Media->MediaId)
		return EFI_MEDIA_CHANGED;

	if (Buffer == NULL)
		return EFI_INVALID_PARAMETER;

	if (BufferSize == 0)
		return EFI_SUCCESS;

	BlockSize = Media->BlockSize;
	if ((BufferSize % BlockSize) != 0)
		return EFI_BAD_BUFFER_SIZE;

	NumberOfBlocks  = BufferSize / BlockSize;
	if ((Lba + NumberOfBlocks - 1) > Media->LastBlock)
		return EFI_INVALID_PARAMETER;

	IoAlign = Media->IoAlign;
	if (IoAlign > 0 && (((UINTN) Buffer & (IoAlign - 1)) != 0))
		return EFI_INVALID_PARAMETER;

	Device = NVME_DEVICE_PRIVATE_DATA_FROM_BLOCK_IO (This);
	Status = NvmeWrite (Device, Buffer, Lba, NumberOfBlocks);

	return Status;
}

/**
  Flush the Block Device.

  @param  This              Indicates a pointer to the calling context.

  @retval EFI_SUCCESS       All outstanding data was written to the device.
  @retval EFI_DEVICE_ERROR  The device reported an error while writing back the data.
  @retval EFI_NO_MEDIA      There is no media in the device.

**/
EFI_STATUS
EFIAPI
NvmeBlockIoFlushBlocks (
	IN  EFI_BLOCK_IO_PROTOCOL   *This
)
{
	NVME_DEVICE_PRIVATE_DATA          *Device;
	EFI_STATUS                        Status;

	//
	// Check parameters.
	//
	if (This == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	Device = NVME_DEVICE_PRIVATE_DATA_FROM_BLOCK_IO (This);

	Status = NvmeFlush (Device);

	return Status;
}

/**
	Trust transfer data from/to NVMe device.

	This function performs one NVMe transaction to do a trust transfer from/to NVMe device.

	@param  Private                    The pointer to the NVME_CONTROLLER_PRIVATE_DATA data structure.
	@param  Buffer                     The pointer to the current transaction buffer.
	@param  SecurityProtocolId         The value of the "Security Protocol" parameter of
	                                   the security protocol command to be sent.
	@param  SecurityProtocolSpecificData The value of the "Security Protocol Specific" parameter
	                                   of the security protocol command to be sent.
	@param  TransferLength             The block number or sector count of the transfer.
	@param  IsTrustSend                Indicates whether it is a trust send operation or not.
	@param  Timeout                    The timeout, in 100ns units, to use for the execution
	                                   of the security protocol command. A Timeout value of 0
	                                   means that this function will wait indefinitely for the
	                                   security protocol command to execute. If Timeout is greater
	                                   than zero, then this function will return EFI_TIMEOUT
	                                   if the time required to execute the receive data command
	                                   is greater than Timeout.
	@param  TransferLengthOut          A pointer to a buffer to store the size in bytes of the data
	                                   written to the buffer. Ignore it when IsTrustSend is TRUE.

	@retval EFI_SUCCESS       The data transfer is complete successfully.
	@return others            Some error occurs when transferring data.

**/
EFI_STATUS
TrustTransferNvmeDevice (
	IN OUT NVME_CONTROLLER_PRIVATE_DATA      *Private,
	IN OUT VOID                              *Buffer,
	IN UINT8                                 SecurityProtocolId,
	IN UINT16                                SecurityProtocolSpecificData,
	IN UINTN                                 TransferLength,
	IN BOOLEAN                               IsTrustSend,
	IN UINT64                                Timeout,
	OUT UINTN                                *TransferLengthOut
)
{
	EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET CommandPacket;
	EFI_NVM_EXPRESS_COMMAND                  Command;
	EFI_NVM_EXPRESS_COMPLETION               Completion;
	EFI_STATUS                               Status;
	UINT16                                   SpecificData;

	ZeroMem (&CommandPacket, sizeof (EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET));
	ZeroMem (&Command, sizeof (EFI_NVM_EXPRESS_COMMAND));
	ZeroMem (&Completion, sizeof (EFI_NVM_EXPRESS_COMPLETION));

	CommandPacket.NvmeCmd        = &Command;
	CommandPacket.NvmeCompletion = &Completion;

	//
	// Change Endianness of SecurityProtocolSpecificData
	//
	SpecificData = (((SecurityProtocolSpecificData << 8) & 0xFF00) | (SecurityProtocolSpecificData >> 8));

	if (IsTrustSend) {
		Command.Cdw0.Opcode          = NVME_ADMIN_SECURITY_SEND_CMD;
		CommandPacket.TransferBuffer = Buffer;
		CommandPacket.TransferLength = (UINT32)TransferLength;
		CommandPacket.NvmeCmd->Cdw10 = (UINT32)((SecurityProtocolId << 24) | (SpecificData << 8));
		CommandPacket.NvmeCmd->Cdw11 = (UINT32)TransferLength;
	} else {
		Command.Cdw0.Opcode          = NVME_ADMIN_SECURITY_RECEIVE_CMD;
		CommandPacket.TransferBuffer = Buffer;
		CommandPacket.TransferLength = (UINT32)TransferLength;
		CommandPacket.NvmeCmd->Cdw10 = (UINT32)((SecurityProtocolId << 24) | (SpecificData << 8));
		CommandPacket.NvmeCmd->Cdw11 = (UINT32)TransferLength;
	}

	CommandPacket.NvmeCmd->Flags = CDW10_VALID | CDW11_VALID;
	CommandPacket.NvmeCmd->Nsid  = NVME_CONTROLLER_ID;
	CommandPacket.CommandTimeout = Timeout;
	CommandPacket.QueueType      = NVME_ADMIN_QUEUE;

	Status = Private->Passthru.PassThru (
		&Private->Passthru,
		NVME_CONTROLLER_ID,
		&CommandPacket,
		NULL
		);

	if (!IsTrustSend) {
		if (EFI_ERROR (Status))  {
			*TransferLengthOut = 0;
		} else {
			*TransferLengthOut = (UINTN) TransferLength;
		}
	}

	return Status;
}

