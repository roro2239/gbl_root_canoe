/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbEntries.c"
#include "../edk2/AndroidToolsPkg/Application/BLTools/BLTools.c"

static EFI_BOOT_SERVICES BootServices;
static EFI_SYSTEM_TABLE SystemTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static QCOM_VERIFIEDBOOT_PROTOCOL VerifiedBoot;
static DeviceInfo Device;
EFI_BOOT_SERVICES *gBS = &BootServices;
EFI_SYSTEM_TABLE *gST = &SystemTable;
EFI_HANDLE gImageHandle;
EFI_GUID gEfiQcomVerifiedBootProtocolGuid, gEfiLoadedImageProtocolGuid;
EFI_GUID gEfiSecurityArchProtocolGuid, gEfiSecurity2ArchProtocolGuid;

static SFB_KEY Keys[16];
static UINTN KeyCount, KeyIndex, Confirmations, Stalls, Resets;
static UINTN Loads, DeviceWrites, Reports, ConfirmationsAtLoad;
static EFI_STATUS ResetStatus, WaitStatus;
static CHAR16 Markers[8];
static UINTN DrawnRows;
static EFI_DEVICE_PATH_PROTOCOL *ExpectedPath;

UINTN EFIAPI __StrLen (CONST CHAR16 *Text) {
  UINTN Size = 0;
  while (Text[Size]) Size++;
  return Size;
}
UINTN EFIAPI __StrSize (CONST CHAR16 *Text) { return (StrLen (Text) + 1) * sizeof (*Text); }
RETURN_STATUS EFIAPI __StrnCpyS (CHAR16 *Out, UINTN Size, CONST CHAR16 *Text, UINTN Count) {
  UINTN Index = 0;
  while (Index < Count && Text[Index]) { assert (Index + 1 < Size); Out[Index] = Text[Index]; Index++; }
  Out[Index] = 0;
  return RETURN_SUCCESS;
}
RETURN_STATUS EFIAPI __StrCpyS (CHAR16 *Out, UINTN Size, CONST CHAR16 *Text) {
  return __StrnCpyS (Out, Size, Text, StrLen (Text));
}

INTN EFIAPI StrnCmp (CONST CHAR16 *A, CONST CHAR16 *B, UINTN Count) {
  for (UINTN Index = 0; Index < Count; Index++) {
    if (A[Index] != B[Index]) return (INTN)A[Index] - B[Index];
    if (!A[Index]) break;
  }
  return 0;
}
INTN EFIAPI StrCmp (CONST CHAR16 *A, CONST CHAR16 *B) { return StrnCmp (A, B, MAX_UINTN); }
VOID *EFIAPI AllocateZeroPool (UINTN Size) { return calloc (1, Size); }
VOID EFIAPI FreePool (VOID *Buffer) { free (Buffer); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memmove (Out, In, Size); }
VOID *EFIAPI SetMem (VOID *Out, UINTN Size, UINT8 Value) { return memset (Out, Value, Size); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp (A, B, Size); }

UINTN EFIAPI UnicodeSPrint (CHAR16 *Out, UINTN Bytes, CONST CHAR16 *Format, ...) {
  va_list Args;
  UINTN Count = 0;
  va_start (Args, Format);
  while (*Format && Count + 1 < Bytes / sizeof (*Out)) {
    if (*Format != '%') { Out[Count++] = *Format++; continue; }
    Format++;
    if (*Format == 's') {
      CONST CHAR16 *Text = va_arg (Args, CONST CHAR16 *);
      while (*Text && Count + 1 < Bytes / sizeof (*Out)) Out[Count++] = *Text++;
      Format++;
    } else {
      CHAR8 Number[32];
      if (*Format == 'u') { snprintf (Number, sizeof (Number), "%u", va_arg (Args, UINT32)); Format++; }
      else {
        assert (Format[0] == 'l' && Format[1] == 'x');
        snprintf (Number, sizeof (Number), "%llx", (unsigned long long)va_arg (Args, UINT64));
        Format += 2;
      }
      for (UINTN Index = 0; Number[Index] && Count + 1 < Bytes / sizeof (*Out); Index++) Out[Count++] = Number[Index];
    }
  }
  va_end (Args);
  Out[Count] = 0;
  return Count;
}

VOID AtUiSetStatusLine (CONST CHAR16 *Text) { (void)Text; }
VOID AtUiBeginScreen (CONST CHAR16 *Title, CONST CHAR16 *Subtitle) {
  (void)Title; (void)Subtitle; DrawnRows = 0; memset (Markers, 0, sizeof (Markers));
}
VOID AtUiEndScreen (CONST CHAR16 *Footer) { (void)Footer; }
VOID AtUiDrawRow (BOOLEAN Selected, CONST CHAR16 *Marker, CONST CHAR16 *Text) {
  (void)Selected; (void)Text; assert (DrawnRows < ARRAY_SIZE (Markers)); Markers[DrawnRows++] = Marker[0];
}
VOID AtUiDrawNote (CONST CHAR16 *Text) { (void)Text; }
VOID AtUiShowMessage (CONST CHAR16 *Text) { (void)Text; }
VOID AtUiShowBanner (CONST CHAR16 *Text, BOOLEAN Clear) { (void)Text; (void)Clear; }
VOID AtUiReportStatus (CONST CHAR16 *Text, EFI_STATUS Status) { (void)Text; assert (EFI_ERROR (Status)); Reports++; }
EFI_STATUS AtUiScreenStatus (VOID) { return EFI_SUCCESS; }
UINTN AtUiVisibleRows (VOID) { return 8; }
AT_KEY AtUiWaitForKey (UINT32 Timeout) { return (AT_KEY)SfbWaitForKey (Timeout); }

static EFI_STATUS EFIAPI stall (UINTN Microseconds) { assert (Microseconds > 0); Stalls++; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI reset_input (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN Extended) {
  assert (This == &Input && !Extended); Resets++; return ResetStatus;
}
static EFI_STATUS EFIAPI wait_event (UINTN Count, EFI_EVENT *Events, UINTN *Index) {
  assert (Count == 1 && Events != NULL); *Index = 0; return WaitStatus;
}
static EFI_STATUS EFIAPI read_key (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key) {
  assert (This == &Input && KeyIndex < KeyCount);
  SFB_KEY Next = Keys[KeyIndex++];
  Key->ScanCode = Next == SfbKeyUp ? SCAN_UP : Next == SfbKeyDown ? SCAN_DOWN : SCAN_NULL;
  Key->UnicodeChar = Next == SfbKeySelect ? CHAR_CARRIAGE_RETURN : 0;
  if (Next == SfbKeySelect) Confirmations++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI device_state (VOID *This, vb_device_state_op_t Operation, UINT8 *Buffer, UINT32 Size) {
  assert (This == &VerifiedBoot && Size == sizeof (Device));
  if (Operation == READ_CONFIG) memcpy (Buffer, &Device, Size);
  else { assert (Confirmations == 3); DeviceWrites++; memcpy (&Device, Buffer, Size); }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI locate_protocol (EFI_GUID *Guid, VOID *Registration, VOID **Interface) {
  (void)Registration;
  if (Guid != &gEfiQcomVerifiedBootProtocolGuid) return EFI_NOT_FOUND;
  *Interface = &VerifiedBoot; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI load_image (BOOLEAN Policy, EFI_HANDLE Parent, EFI_DEVICE_PATH_PROTOCOL *Path,
                                    VOID *Buffer, UINTN Size, EFI_HANDLE *Handle) {
  (void)Policy; (void)Parent; (void)Buffer; (void)Size; (void)Handle;
  assert (Path == ExpectedPath);
  assert (Confirmations == ConfirmationsAtLoad);
  Loads++;
  return EFI_NOT_FOUND;
}

EFI_STATUS SfbSecurityVerifyImage (CONST VOID *Buffer, UINTN Size) {
  (void)Buffer;
  (void)Size;
  return EFI_SECURITY_VIOLATION;
}

static void reset (void) {
  KeyCount = KeyIndex = Confirmations = Stalls = Resets = 0;
  Loads = DeviceWrites = Reports = ConfirmationsAtLoad = 0;
  ResetStatus = WaitStatus = EFI_SUCCESS;
  ExpectedPath = NULL;
  mSubmenuInputPending = FALSE;
}

static void keys (UINTN ConfirmCount, SFB_KEY Last) {
  for (UINTN Index = 0; Index < ConfirmCount; Index++) Keys[KeyCount++] = SfbKeySelect;
  if (Last != SfbKeyTimeout) Keys[KeyCount++] = Last;
}
static void make_entry (SFB_BOOT_ENTRY *Entry, CONST CHAR16 *Name, CONST CHAR16 *Path, UINTN Id) {
  memset (Entry, 0, sizeof (*Entry)); StrCpyS (Entry->Spec.Name, SFB_NAME_CHARS, Name);
  StrCpyS (Entry->Spec.Path, SFB_PATH_CHARS, Path); Entry->Spec.HasAddress = TRUE;
  Entry->Volume = &Fs; Entry->DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)Id;
}
static void boot_list (void) {
  SFB_ENTRY_LIST *List = SfbEntries (); memset (List, 0, sizeof (*List)); List->Count = 4;
  make_entry (&List->Entry[0], L"$Driver", L"\\driver.efi", 1);
  make_entry (&List->Entry[1], L"Android", L"\\efisp\\boot.efi", 2);
  make_entry (&List->Entry[2], L"Android backup", L"\\efisp\\boot_backup.efi", 3);
  make_entry (&List->Entry[3], L"Custom real state", L"\\EFISP\\BOOT_NORMAL.EFI", 4);
}

static void check_boot_launch (void) {
  SFB_ENTRY_LIST *List = SfbEntries ();
  BOOLEAN NotPeImage;

  boot_list ();
  for (UINTN Step = 0; Step < 3; Step++) {
    for (SFB_KEY Cancel = SfbKeyUp; Cancel <= SfbKeyDown; Cancel++) {
      reset ();
      keys (Step, Cancel);
      NotPeImage = TRUE;
      assert (SfbLaunchEntry (&List->Entry[3], TRUE, &NotPeImage) == EFI_ABORTED);
      assert (Confirmations == Step && Loads == 0 && !NotPeImage);
    }
  }

  for (UINTN Index = 1; Index < List->Count; Index++) {
    reset ();
    keys (3, SfbKeyTimeout);
    ExpectedPath = List->Entry[Index].DevicePath;
    ConfirmationsAtLoad = 3;
    assert (SfbLaunchEntry (&List->Entry[Index], TRUE, NULL) == EFI_NOT_FOUND);
    assert (Loads == 1 && Resets == 3 && Stalls == 3);
  }

  reset ();
  ResetStatus = EFI_DEVICE_ERROR;
  assert (SfbLaunchEntry (&List->Entry[1], TRUE, NULL) == EFI_ABORTED);
  assert (Reports == 1 && Loads == 0 && KeyIndex == 0);

  reset ();
  WaitStatus = EFI_DEVICE_ERROR;
  assert (SfbLaunchEntry (&List->Entry[1], TRUE, NULL) == EFI_ABORTED);
  assert (Reports == 1 && Loads == 0 && KeyIndex == 0);

  reset ();
  ExpectedPath = List->Entry[0].DevicePath;
  assert (SfbLaunchEntry (&List->Entry[0], TRUE, NULL) == EFI_NOT_FOUND);
  assert (Loads == 1 && Resets == 0 && KeyIndex == 0);

  reset ();
  ExpectedPath = List->Entry[1].DevicePath;
  assert (SfbLaunchEntry (&List->Entry[1], FALSE, NULL) == EFI_NOT_FOUND);
  assert (Loads == 1 && Resets == 0 && KeyIndex == 0);
}

static void check_markers (void) {
  SFB_MENU_ROW Rows[SFB_MAX_MENU_ROWS];
  SFB_ENTRY_LIST *List = SfbEntries ();
  UINTN Count, Default;

  reset ();
  boot_list ();
  Count = SfbBuildRows (List, L"Boot Modes", Rows);
  Default = SfbDefaultBootModeIndex (List);
  assert (Default == 1);
  for (UINTN Cursor = 0; Cursor < 2; Cursor++) {
    SfbDrawMenu (Rows, Count, Cursor, Default, L"启动方式");
    assert (Markers[0] == L'✓' && Markers[1] == L' ');
  }

  make_entry (&List->Entry[1], L"其他程序", L"\\other.efi", 2);
  assert (SfbDefaultBootModeIndex (List) == 3);
  List->Count = 3;
  assert (SfbDefaultBootModeIndex (List) == SFB_NO_INDEX);
}

static void check_bl_write (void) {
  DeviceInfo Info = {0};
  Info.is_unlocked = TRUE;
  for (UINTN Step = 0; Step < 3; Step++) {
    for (SFB_KEY Cancel = SfbKeyUp; Cancel <= SfbKeyDown; Cancel++) {
      reset ();
      keys (Step, Cancel);
      BlApply (&Info, FALSE, FALSE, L"锁定设备");
      assert (DeviceWrites == 0 && Info.is_unlocked && Confirmations == Step);
    }
  }
  reset ();
  ResetStatus = EFI_DEVICE_ERROR;
  BlApply (&Info, FALSE, FALSE, L"锁定设备");
  assert (DeviceWrites == 0 && Info.is_unlocked && Reports == 1);

  reset ();
  keys (3, SfbKeyDown);
  BlApply (&Info, FALSE, FALSE, L"锁定设备");
  assert (DeviceWrites == 1 && !Info.is_unlocked && Confirmations == 3 && Resets == 3);
}

int main (void) {
  BootServices.Stall = stall;
  BootServices.WaitForEvent = wait_event;
  BootServices.LocateProtocol = locate_protocol;
  BootServices.LoadImage = load_image;
  Input.Reset = reset_input;
  Input.ReadKeyStroke = read_key;
  SystemTable.ConIn = &Input;
  VerifiedBoot.VBRwDeviceState = device_state;
  memcpy (Device.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
  check_boot_launch ();
  check_markers ();
  check_bl_write ();
  FreePool (mSfbEntries);
  puts ("手动启动和 BL 状态修改的三次确认、取消保护及默认启动项标记测试通过");
  return 0;
}
