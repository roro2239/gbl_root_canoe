/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include <AtDevInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SfbConfigStore.h>
#include <Library/UefiBootServicesTableLib.h>
#include "SuperFbBootOptions.h"

static CONST CHAR8 *Record;
static EFI_STATUS StoreStatus;
static EFI_STATUS ProtocolStatus;
static EFI_STATUS StateStatus;
static DeviceInfo Device;
static EFI_FILE_PROTOCOL Root;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
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

EFI_STATUS SfbConfigStoreGetFs (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL **OutFs, EFI_HANDLE *Volume) {
  (void)Volume;
  if (!EFI_ERROR (StoreStatus)) *OutFs = &Fs;
  return StoreStatus;
}

static EFI_STATUS EFIAPI open_volume (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **OutRoot) {
  (void)This;
  *OutRoot = &Root;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI close_root (EFI_FILE_PROTOCOL *This) {
  (void)This;
  return EFI_SUCCESS;
}

EFI_STATUS SfbReadFileBytes (EFI_FILE_PROTOCOL *InRoot, CONST CHAR16 *Path, VOID *Buffer,
                            UINTN MaxBytes, UINTN *BytesRead) {
  (void)Path;
  assert (InRoot == &Root);
  if (Record == NULL) return EFI_NOT_FOUND;
  *BytesRead = MIN (strlen (Record), MaxBytes);
  memcpy (Buffer, Record, *BytesRead);
  return EFI_SUCCESS;
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
  SFB_BOOT_OPTIONS Options;
  BOOLEAN Unlocked;
  Fs.OpenVolume = open_volume;
  Root.Close = close_root;

  StoreStatus = EFI_NOT_FOUND;
  assert (SfbLoadBootOptions (&Options) == EFI_SUCCESS);
  assert (!Options.BootWarning && Options.FixFastboot && !Options.ShowUiHints);
  StoreStatus = EFI_SUCCESS;
  assert (SfbLoadBootOptions (&Options) == EFI_SUCCESS);
  assert (!Options.BootWarning && Options.FixFastboot && !Options.ShowUiHints);
  Record = "SFBO1|1|0";
  assert (SfbLoadBootOptions (&Options) == EFI_SUCCESS);
  assert (Options.BootWarning && !Options.FixFastboot && !Options.ShowUiHints);
  for (int Warning = 0; Warning < 2; Warning++) {
    for (int Fastboot = 0; Fastboot < 2; Fastboot++) {
      for (int Show = 0; Show < 2; Show++) {
        CHAR8 Text[] = "SFBO2|0|1|0";
        Text[6] = '0' + Warning;
        Text[8] = '0' + Fastboot;
        Text[10] = '0' + Show;
        Record = Text;
        assert (SfbLoadBootOptions (&Options) == EFI_SUCCESS);
        assert (Options.BootWarning == Warning && Options.FixFastboot == Fastboot && Options.ShowUiHints == Show);
      }
    }
  }
  Record = "SFBO2|0|1|x";
  assert (SfbLoadBootOptions (&Options) == EFI_VOLUME_CORRUPTED);
  Record = "SFBO1|0|1|0";
  assert (SfbLoadBootOptions (&Options) == EFI_VOLUME_CORRUPTED);

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
  puts ("提示默认关闭、旧配置兼容、独立开关和真实 BL 状态读取测试通过");
  return 0;
}
