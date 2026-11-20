/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include <AtDevInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "SuperFbMenu.h"

static EFI_STATUS ProtocolStatus;
static EFI_STATUS StateStatus;
static DeviceInfo Device;
static QCOM_VERIFIEDBOOT_PROTOCOL VerifiedBoot;
static EFI_BOOT_SERVICES BootServices;
EFI_BOOT_SERVICES *gBS = &BootServices;
EFI_GUID gEfiQcomVerifiedBootProtocolGuid;

INTN EFIAPI CompareMem (CONST VOID *First, CONST VOID *Second, UINTN Size) {
  return memcmp (First, Second, Size);
}

VOID *EFIAPI SetMem (VOID *Buffer, UINTN Size, UINT8 Value) {
  return memset (Buffer, Value, Size);
}

static EFI_STATUS EFIAPI locate_protocol (EFI_GUID *Guid, VOID *Registration, VOID **Interface) {
  (void)Registration;
  assert (Guid == &gEfiQcomVerifiedBootProtocolGuid);
  if (!EFI_ERROR (ProtocolStatus)) *Interface = &VerifiedBoot;
  return ProtocolStatus;
}

static EFI_STATUS EFIAPI read_state (VOID *This, vb_device_state_op_t Operation, UINT8 *Buffer, UINT32 Size) {
  assert (This == &VerifiedBoot && Operation == READ_CONFIG && Size == sizeof (DeviceInfo));
  if (!EFI_ERROR (StateStatus)) memcpy (Buffer, &Device, Size);
  return StateStatus;
}

int main (void) {
  BOOLEAN Unlocked;

  BootServices.LocateProtocol = locate_protocol;
  VerifiedBoot.VBRwDeviceState = read_state;
  memcpy (Device.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
  Device.is_unlocked = FALSE;
  assert (SfbReadBlState (&Unlocked) == EFI_SUCCESS && !Unlocked);
  Device.is_unlocked = TRUE;
  assert (SfbReadBlState (&Unlocked) == EFI_SUCCESS && Unlocked);
  Device.is_unlocked = 2;
  assert (SfbReadBlState (&Unlocked) == EFI_VOLUME_CORRUPTED);
  StateStatus = EFI_DEVICE_ERROR;
  assert (SfbReadBlState (&Unlocked) == EFI_DEVICE_ERROR);
  ProtocolStatus = EFI_NOT_FOUND;
  assert (SfbReadBlState (&Unlocked) == EFI_NOT_FOUND);
  puts ("真实 BL 状态及读取错误处理测试通过");
  return 0;
}
