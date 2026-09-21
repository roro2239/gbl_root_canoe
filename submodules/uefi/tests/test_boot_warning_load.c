/* 验证生产加载流程的开关传递、真实锁状态判断与只读文件处理。 */
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbEntries.c"

static BOOLEAN enabled, unlocked;
static EFI_STATUS setting_status, lock_status, open_status, patch_status, close_status;
static UINT64 length, position;
static UINTN opens, closes, reads, patches;
static BOOLEAN short_read, zero_read;
static EFI_FILE_PROTOCOL root, file;
static const UINT8 payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

EFI_STATUS SfbLoadBootWarning (BOOLEAN *value) { *value = enabled; return setting_status; }
EFI_STATUS SfbReadBlState (BOOLEAN *value) { *value = unlocked; return lock_status; }
CONST CHAR16 *SfbVolumeRootPrefix (EFI_HANDLE volume) { (void)volume; return L"\\efisp"; }
EFI_STATUS SfbOpenVolumeRoot (EFI_HANDLE volume, EFI_FILE_PROTOCOL **value) {
  (void)volume; *value = &root; return EFI_SUCCESS;
}
EFI_STATUS SfbEnableBootWarning (VOID *data, UINTN size) {
  patches++;
  assert (size == sizeof (payload) && memcmp (data, payload, size) == 0);
  if (!EFI_ERROR (patch_status)) ((UINT8 *)data)[0] = 42;
  return patch_status;
}
static EFI_STATUS EFIAPI open_file (EFI_FILE_PROTOCOL *self, EFI_FILE_PROTOCOL **out,
                                    CHAR16 *path, UINT64 mode, UINT64 attributes) {
  assert (self == &root && StrCmp (path, L"\\efisp\\boot.efi") == 0);
  assert (mode == EFI_FILE_MODE_READ && attributes == 0);
  opens++;
  if (EFI_ERROR (open_status)) return open_status;
  *out = &file;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI close_file (EFI_FILE_PROTOCOL *self) {
  closes++;
  return self == &file ? close_status : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI set_position (EFI_FILE_PROTOCOL *self, UINT64 pos) {
  assert (self == &file);
  position = pos == MAX_UINT64 ? length : pos;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI get_position (EFI_FILE_PROTOCOL *self, UINT64 *pos) {
  assert (self == &file);
  *pos = position;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_file (EFI_FILE_PROTOCOL *self, UINTN *size, VOID *buffer) {
  assert (self == &file && position + *size <= sizeof (payload));
  reads++;
  if (zero_read) *size = 0;
  else if (short_read && *size > 2) *size = 2;
  memcpy (buffer, payload + position, *size);
  position += *size;
  return EFI_SUCCESS;
}
static void reset (void) {
  enabled = unlocked = TRUE;
  setting_status = lock_status = open_status = patch_status = close_status = EFI_SUCCESS;
  opens = closes = reads = patches = 0;
  length = sizeof (payload);
  position = 0;
  short_read = zero_read = FALSE;
}

int main (void) {
  VOID *buffer;
  UINTN size;
  SFB_BOOT_ENTRY entry = {0};
  entry.Kind = SfbEntryEfiFile;
  entry.Volume = &entry;
  StrnCpyS (entry.Path, SFB_PATH_CHARS, L"\\efisp\\boot.efi", SFB_PATH_CHARS - 1);
  root.Open = open_file;
  root.Close = close_file;
  file.Close = close_file;
  file.SetPosition = set_position;
  file.GetPosition = get_position;
  file.Read = read_file;
  reset ();
  enabled = FALSE;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_SUCCESS);
  assert (buffer == NULL && size == 0 && opens == 0 && patches == 0);
  reset ();
  unlocked = FALSE;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_SUCCESS && buffer == NULL);
  assert (opens == 0 && patches == 0);
  reset ();
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_SUCCESS);
  assert (size == 8 && ((UINT8 *)buffer)[0] == 42 && payload[0] == 1);
  assert (opens == 1 && closes == 2 && reads == 1 && patches == 1);
  FreePool (buffer);
  reset ();
  short_read = TRUE;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_SUCCESS);
  assert (reads == 4 && patches == 1 && closes == 2);
  FreePool (buffer);
  reset ();
  setting_status = EFI_DEVICE_ERROR;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_DEVICE_ERROR && buffer == NULL);
  assert (opens == 0);
  reset ();
  lock_status = EFI_DEVICE_ERROR;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_DEVICE_ERROR && buffer == NULL);
  assert (opens == 0);
  reset ();
  open_status = EFI_NOT_FOUND;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_NOT_FOUND && buffer == NULL);
  assert (closes == 1);
  reset ();
  length = SIZE_32MB + 1;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_BAD_BUFFER_SIZE && buffer == NULL);
  assert (closes == 2 && patches == 0);
  reset ();
  zero_read = TRUE;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_DEVICE_ERROR && buffer == NULL);
  assert (closes == 2 && patches == 0);
  reset ();
  close_status = EFI_DEVICE_ERROR;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_DEVICE_ERROR && buffer == NULL);
  assert (closes == 2 && patches == 0);
  reset ();
  patch_status = EFI_UNSUPPORTED;
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_UNSUPPORTED && buffer == NULL);
  assert (closes == 2 && patches == 1);
  reset ();
  StrnCpyS (entry.Path, SFB_PATH_CHARS, L"\\efisp\\tools\\BLTools.efi", SFB_PATH_CHARS - 1);
  assert (SfbPrepareWarningImage (&entry, &buffer, &size) == EFI_SUCCESS && buffer == NULL);
  assert (opens == 0 && patches == 0);
  puts ("加载流程的开关、锁状态、短读及失败资源释放测试通过");
  return 0;
}
