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
static EFI_FILE_PROTOCOL Root, File, ImageFile;
static EFI_LOADED_IMAGE_PROTOCOL Loaded;
static QCOM_VERIFIEDBOOT_PROTOCOL VerifiedBoot;
static DeviceInfo Device;
EFI_BOOT_SERVICES *gBS = &BootServices;
EFI_SYSTEM_TABLE *gST = &SystemTable;
EFI_HANDLE gImageHandle;
EFI_GUID gEfiQcomVerifiedBootProtocolGuid, gEfiFileInfoGuid;
EFI_GUID gEfiSecurityArchProtocolGuid, gEfiSecurity2ArchProtocolGuid;
EFI_GUID gEfiBlockIoProtocolGuid, gEfiLoadedImageProtocolGuid;

static SFB_KEY Keys[16];
static UINTN KeyCount, KeyIndex, Confirmations, Stalls, Resets;
static UINTN ImageOpens, Loads, Starts, Writes, DeviceWrites, Reports;
static EFI_STATUS ResetStatus, WaitStatus, WriteStatus;
static CHAR8 OptionsRecord[12] = "SFBO2|0|1|0";
static CHAR8 EntriesRecord[SFB_ENTRIES_MAX_BYTES + 1];
static CHAR16 FilePath[SFB_PATH_CHARS];
static CHAR16 Markers[8];
static CHAR16 DrawnTitle[SFB_NAME_CHARS + 80];
static CONST CHAR16 *StatusLine;
static UINTN DrawnRows;
static UINTN Banners;
static BOOLEAN BannerCleared;
static UINTN ImagePosition;
static EFI_DEVICE_PATH_PROTOCOL *ExpectedPath;
static CONST CHAR16 *ExpectedArgs;

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
UINTN EFIAPI __AsciiStrLen (CONST CHAR8 *Text) { return strlen (Text); }

INTN EFIAPI StrnCmp (CONST CHAR16 *A, CONST CHAR16 *B, UINTN Count) {
  for (UINTN Index = 0; Index < Count; Index++) {
    if (A[Index] != B[Index]) return (INTN)A[Index] - B[Index];
    if (!A[Index]) break;
  }
  return 0;
}
INTN EFIAPI StrCmp (CONST CHAR16 *A, CONST CHAR16 *B) { return StrnCmp (A, B, MAX_UINTN); }
VOID *EFIAPI AllocatePool (UINTN Size) { return malloc (Size); }
VOID *EFIAPI AllocateZeroPool (UINTN Size) { return calloc (1, Size); }
VOID EFIAPI FreePool (VOID *Buffer) { free (Buffer); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memmove (Out, In, Size); }
VOID *EFIAPI ZeroMem (VOID *Out, UINTN Size) { return memset (Out, 0, Size); }
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

VOID AtUiSetStatusLine (CONST CHAR16 *Text) { StatusLine = Text; }
VOID AtUiBeginScreen (CONST CHAR16 *Title, CONST CHAR16 *Subtitle) {
  (void)Subtitle; StrCpyS (DrawnTitle, ARRAY_SIZE (DrawnTitle), Title);
  DrawnRows = 0; memset (Markers, 0, sizeof (Markers));
}
VOID AtUiEndScreen (CONST CHAR16 *Footer) { (void)Footer; }
VOID AtUiDrawRow (BOOLEAN Selected, CONST CHAR16 *Marker, CONST CHAR16 *Text) {
  (void)Selected; (void)Text; assert (DrawnRows < ARRAY_SIZE (Markers)); Markers[DrawnRows++] = Marker[0];
}
VOID AtUiDrawNote (CONST CHAR16 *Text) { (void)Text; }
VOID AtUiShowMessage (CONST CHAR16 *Text) { (void)Text; }
VOID AtUiShowBanner (CONST CHAR16 *Text, BOOLEAN Clear) {
  StrCpyS (DrawnTitle, ARRAY_SIZE (DrawnTitle), Text);
  Banners++;
  BannerCleared = Clear;
}
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
  else { assert (Confirmations >= 3); DeviceWrites++; memcpy (&Device, Buffer, Size); }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI locate_protocol (EFI_GUID *Guid, VOID *Registration, VOID **Interface) {
  (void)Registration;
  if (Guid != &gEfiQcomVerifiedBootProtocolGuid) return EFI_NOT_FOUND;
  *Interface = &VerifiedBoot; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI handle_protocol (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface) {
  (void)Handle; (void)Guid; (void)Interface; return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI open_protocol (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface,
                                        EFI_HANDLE Agent, EFI_HANDLE Controller, UINT32 Attributes) {
  (void)Handle; (void)Agent; (void)Controller; (void)Attributes;
  assert (Guid == &gEfiLoadedImageProtocolGuid); *Interface = &Loaded; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI load_image (BOOLEAN Policy, EFI_HANDLE Parent, EFI_DEVICE_PATH_PROTOCOL *Path,
                                     VOID *Buffer, UINTN Size, EFI_HANDLE *Handle) {
  (void)Policy; (void)Parent; (void)Buffer; (void)Size;
  assert (Path == ExpectedPath); Loads++; *Handle = &Loaded; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI start_image (EFI_HANDLE Handle, UINTN *ExitSize, CHAR16 **ExitData) {
  (void)ExitSize; (void)ExitData; assert (Handle == &Loaded); Starts++;
  if (ExpectedArgs != NULL) assert (StrCmp (Loaded.LoadOptions, ExpectedArgs) == 0);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI close_file (EFI_FILE_PROTOCOL *This) { (void)This; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI open_volume (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Out) {
  assert (This == &Fs); *Out = &Root; return EFI_SUCCESS;
}
EFI_STATUS SfbConfigStoreGetFs (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL **Out, EFI_HANDLE *Volume) {
  if (Out != NULL) *Out = &Fs;
  if (Volume != NULL) *Volume = &Fs;
  return EFI_SUCCESS;
}
EFI_STATUS SfbReadFileBytes (EFI_FILE_PROTOCOL *This, CONST CHAR16 *Path, VOID *Buffer, UINTN Max, UINTN *Size) {
  assert (This == &Root && StrCmp (Path, SFB_BOOT_OPTIONS_PATH) == 0 && Max >= 11);
  *Size = 11; memcpy (Buffer, OptionsRecord, *Size); return EFI_SUCCESS;
}
EFI_STATUS SfbOpenVolumeRoot (EFI_HANDLE Volume, EFI_FILE_PROTOCOL **Out) {
  assert (Volume != NULL); ImageOpens++; *Out = &Root; return EFI_SUCCESS;
}
EFI_STATUS SfbSecurityVerifyImage (CONST VOID *Buffer, UINTN Size) { assert (Buffer && Size == 8); return EFI_SUCCESS; }
EFI_STATUS SfbPatchBootImage (VOID *Buffer, UINTN Size, CONST SFB_BOOT_OPTIONS *Options) {
  (void)Options; assert (Buffer && Size == 8); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_file (EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **Out,
                                    CHAR16 *Path, UINT64 Mode, UINT64 Attributes) {
  (void)Attributes; assert (This == &Root);
  if (Mode == EFI_FILE_MODE_READ) {
    *Out = &ImageFile; ImagePosition = 0; return EFI_SUCCESS;
  }
  if (!(Mode & EFI_FILE_MODE_CREATE) && StrCmp (Path, SFB_STORE_TEMP_PATH) != 0 &&
      StrCmp (Path, SFB_BOOT_OPTIONS_TEMP) != 0) return EFI_NOT_FOUND;
  StrCpyS (FilePath, ARRAY_SIZE (FilePath), Path); *Out = &File; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_file (EFI_FILE_PROTOCOL *This, UINTN *Size, VOID *Buffer) {
  assert (This == &File); Writes++;
  if (EFI_ERROR (WriteStatus)) return WriteStatus;
  if (StrCmp (FilePath, SFB_BOOT_OPTIONS_TEMP) == 0) {
    assert (*Size == 11); memcpy (OptionsRecord, Buffer, *Size);
  } else {
    assert (*Size < sizeof (EntriesRecord)); memcpy (EntriesRecord, Buffer, *Size); EntriesRecord[*Size] = 0;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI get_info (EFI_FILE_PROTOCOL *This, EFI_GUID *Guid, UINTN *Size, VOID *Buffer) {
  assert (This == &File && Guid == &gEfiFileInfoGuid);
  if (Buffer == NULL) { *Size = SIZE_OF_EFI_FILE_INFO + 256; return EFI_BUFFER_TOO_SMALL; }
  memset (Buffer, 0, *Size); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI set_info (EFI_FILE_PROTOCOL *This, EFI_GUID *Guid, UINTN Size, VOID *Buffer) {
  (void)Size; (void)Buffer; assert (This == &File && Guid == &gEfiFileInfoGuid); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI set_position (EFI_FILE_PROTOCOL *This, UINT64 Position) {
  assert (This == &ImageFile); ImagePosition = Position == MAX_UINT64 ? 8 : Position; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI get_position (EFI_FILE_PROTOCOL *This, UINT64 *Position) {
  assert (This == &ImageFile); *Position = ImagePosition; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_file (EFI_FILE_PROTOCOL *This, UINTN *Size, VOID *Buffer) {
  assert (This == &ImageFile && ImagePosition + *Size <= 8); memset (Buffer, 0, *Size); ImagePosition += *Size; return EFI_SUCCESS;
}

static void reset (void) {
  KeyCount = KeyIndex = Confirmations = Stalls = Resets = 0;
  ImageOpens = Loads = Starts = Writes = DeviceWrites = Reports = 0;
  Banners = 0;
  BannerCleared = FALSE;
  ResetStatus = WaitStatus = WriteStatus = EFI_SUCCESS;
  memset (&Loaded, 0, sizeof (Loaded));
  memset (EntriesRecord, 0, sizeof (EntriesRecord));
  memcpy (OptionsRecord, "SFBO2|0|1|0", sizeof (OptionsRecord));
  mSubmenuInputPending = FALSE;
  ExpectedArgs = NULL;
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
  StrCpyS (List->Entry[3].Spec.Args, SFB_ARGS_CHARS, L"real-state");
}

static void check_boot_launch (void) {
  SFB_ENTRY_LIST *List = SfbEntries ();
  for (UINTN Step = 0; Step < 3; Step++) {
    for (SFB_KEY Cancel = SfbKeyUp; Cancel <= SfbKeyDown; Cancel++) {
      reset (); boot_list (); keys (Step, Cancel);
      assert (SfbLaunchEntry (&List->Entry[3], TRUE, NULL) == EFI_ABORTED);
      assert (ImageOpens == 0 && Loads == 0 && Writes == 0 && Starts == 0);
      assert (SfbGetBootMode (&List->Entry[1].Spec) == SfbBootModeFakeLocked);
    }
  }
  reset (); boot_list (); keys (3, SfbKeyTimeout); ExpectedPath = List->Entry[3].DevicePath; ExpectedArgs = L"real-state";
  assert (SfbLaunchEntry (&List->Entry[3], TRUE, NULL) == EFI_SUCCESS);
  assert (Confirmations == 3 && Resets == 3 && ImageOpens == 1 && Writes == 1 && Loads == 1 && Starts == 1);
  assert (Banners == 1 && BannerCleared && DrawnTitle[0] == 0 && StatusLine == NULL);
  assert (SfbGetBootMode (&List->Entry[1].Spec) == SfbBootModeRealState);
  assert (List->Entry[0].DevicePath == (VOID *)1 && List->Entry[2].DevicePath == (VOID *)3);
  assert (strstr (EntriesRecord, "BOOT_NORMAL.EFI") < strstr (EntriesRecord, "boot.efi"));
  reset (); keys (3, SfbKeyTimeout); ExpectedPath = List->Entry[3].DevicePath;
  assert (SfbLaunchEntry (&List->Entry[3], TRUE, NULL) == EFI_SUCCESS);
  assert (SfbGetBootMode (&List->Entry[1].Spec) == SfbBootModeFakeLocked && Writes == 1);

  reset (); boot_list (); keys (3, SfbKeyTimeout); WriteStatus = EFI_DEVICE_ERROR;
  assert (SfbLaunchEntry (&List->Entry[3], TRUE, NULL) == EFI_ABORTED);
  assert (Loads == 0 && Starts == 0 && Reports == 1);
  assert (SfbGetBootMode (&List->Entry[1].Spec) == SfbBootModeFakeLocked);
  reset (); keys (3, SfbKeyTimeout); ExpectedPath = List->Entry[2].DevicePath;
  assert (SfbLaunchEntry (&List->Entry[2], TRUE, NULL) == EFI_SUCCESS && Writes == 0);
  reset (); ExpectedPath = List->Entry[1].DevicePath;
  assert (SfbLaunchEntry (&List->Entry[1], FALSE, NULL) == EFI_SUCCESS);
  assert (KeyIndex == 0 && Writes == 0 && Loads == 1);
  reset (); ResetStatus = EFI_DEVICE_ERROR;
  assert (SfbLaunchEntry (&List->Entry[3], TRUE, NULL) == EFI_ABORTED && Reports == 1 && Loads == 0);

  reset (); boot_list ();
  BOOLEAN BootModeTried = FALSE, NotPeImage = FALSE;
  ExpectedPath = List->Entry[1].DevicePath;
  assert (SfbStartupLaunch (&List->Entry[2], &NotPeImage, &BootModeTried) == EFI_ABORTED);
  assert (!BootModeTried && Loads == 0);
  assert (SfbStartupLaunch (&List->Entry[1], &NotPeImage, &BootModeTried) == EFI_SUCCESS);
  assert (SfbStartupLaunch (&List->Entry[3], &NotPeImage, &BootModeTried) == EFI_ABORTED);
  assert (SfbStartupLaunch (&List->Entry[2], &NotPeImage, &BootModeTried) == EFI_ABORTED);
  assert (!NotPeImage && Loads == 1 && Writes == 0 && KeyIndex == 0);
}

static void check_boot_hints (void) {
  SFB_ENTRY_LIST *List = SfbEntries ();

  reset (); boot_list ();
  for (UINTN Index = 1; Index < List->Count; Index++) {
    ExpectedPath = List->Entry[Index].DevicePath;
    assert (SfbLaunchEntry (&List->Entry[Index], FALSE, NULL) == EFI_SUCCESS);
    assert (Banners == 0 && StatusLine == NULL);
  }
  assert (Loads == 3 && Starts == 3 && Writes == 0 && KeyIndex == 0);

  reset ();
  OptionsRecord[10] = '1';
  ExpectedPath = List->Entry[1].DevicePath;
  assert (SfbLaunchEntry (&List->Entry[1], FALSE, NULL) == EFI_SUCCESS);
  assert (Banners == 1 && !BannerCleared);
  assert (StrCmp (DrawnTitle, L"正在启动 Android（假回锁）") == 0);
  assert (StrCmp (StatusLine, L"BL 真实状态：已锁定") == 0);
}

static void check_menu_title (void) {
  CHAR16 Expected[SFB_NAME_CHARS + 80];

  Device.is_unlocked = TRUE;
  AtUiSetStatusLine (L"旧状态栏");
  SfbBeginScreen (NULL, NULL);
  assert (StrCmp (DrawnTitle, L"假回锁-已解锁") == 0 && StatusLine == NULL);
  SfbBeginScreen (L"启动方式", NULL);
  assert (StrCmp (DrawnTitle, L"启动方式-已解锁") == 0 && StatusLine == NULL);
  Device.is_unlocked = FALSE;
  SfbBeginScreen (NULL, NULL);
  assert (StrCmp (DrawnTitle, L"假回锁-未解锁") == 0 && StatusLine == NULL);

  Device.is_unlocked = 2;
  SfbBeginScreen (NULL, NULL);
  UnicodeSPrint (Expected, sizeof (Expected), L"假回锁-BL 真实状态：读取失败（0x%lx）",
                  (UINT64)EFI_VOLUME_CORRUPTED);
  assert (StrCmp (DrawnTitle, Expected) == 0 && StatusLine == NULL);
  Device.is_unlocked = FALSE;
}

static void check_markers (void) {
  SFB_MENU_ROW Rows[SFB_MAX_MENU_ROWS];
  SFB_ENTRY_LIST *List = SfbEntries ();
  reset (); boot_list ();
  UINTN Count = SfbBuildRows (List, L"Boot Modes", Rows);
  SfbDrawMenu (Rows, Count, 1, SfbDefaultBootModeIndex (List), L"启动方式");
  assert (Markers[0] == L'✓' && Markers[1] == L' ');
  keys (0, SfbKeyUp); keys (1, SfbKeyTimeout);
  SfbRunBootOptions ();
  assert (Markers[0] == L' ' && Markers[1] == L'✓' && Markers[2] == L' ');
  reset (); keys (1, SfbKeyUp); keys (1, SfbKeyTimeout);
  SfbRunBootOptions ();
  assert (Markers[0] == L'✓' && Markers[1] == L'✓' && OptionsRecord[6] == '1');
  reset (); WriteStatus = EFI_DEVICE_ERROR; keys (1, SfbKeyUp); keys (1, SfbKeyTimeout);
  SfbRunBootOptions ();
  assert (Markers[0] == L' ' && Reports == 1 && OptionsRecord[6] == '0');
}

static void check_bl_write (void) {
  DeviceInfo Info = {0};
  for (UINTN Step = 0; Step < 3; Step++) {
    reset (); keys (Step, SfbKeyDown); BlApply (&Info, TRUE, FALSE, L"解锁设备");
    assert (DeviceWrites == 0 && !Info.is_unlocked);
  }
  reset (); keys (4, SfbKeyTimeout); BlApply (&Info, TRUE, FALSE, L"解锁设备");
  assert (DeviceWrites == 1 && Info.is_unlocked && Confirmations == 4);
  reset (); ResetStatus = EFI_DEVICE_ERROR; BlApply (&Info, FALSE, FALSE, L"锁定设备");
  assert (DeviceWrites == 0 && Info.is_unlocked && Reports == 1);
}

int main (void) {
  BootServices.Stall = stall; BootServices.WaitForEvent = wait_event; BootServices.LocateProtocol = locate_protocol;
  BootServices.HandleProtocol = handle_protocol; BootServices.OpenProtocol = open_protocol;
  BootServices.LoadImage = load_image; BootServices.StartImage = start_image;
  Input.Reset = reset_input; Input.ReadKeyStroke = read_key; SystemTable.ConIn = &Input;
  VerifiedBoot.VBRwDeviceState = device_state; memcpy (Device.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
  Fs.OpenVolume = open_volume; Root.Open = open_file; Root.Close = Root.Flush = close_file;
  File.Close = File.Flush = File.Delete = close_file; File.Write = write_file; File.GetInfo = get_info; File.SetInfo = set_info;
  ImageFile.Close = close_file; ImageFile.SetPosition = set_position; ImageFile.GetPosition = get_position; ImageFile.Read = read_file;
  check_menu_title (); check_boot_hints (); check_boot_launch (); check_markers (); check_bl_write ();
  FreePool (mSfbEntries);
  puts ("关闭启动提示、标题合并真实 BL 状态、启动方式确认与保存、选项勾选和 BL 写入确认测试通过");
  return 0;
}
