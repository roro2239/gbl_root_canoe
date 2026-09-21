/* 主机测试中的 UEFI 库与块设备替身；不访问真实设备。 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/Gpt.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>
#include <stdlib.h>
#include <string.h>

EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiPartTypeSystemPartGuid;
EFI_GUID gEfiBlockIoProtocolGuid;
EFI_GUID gEfiPartitionInfoProtocolGuid;

UINT16 EFIAPI ReadUnaligned16 (CONST UINT16 *p) { UINT16 v; memcpy (&v, p, 2); return v; }
UINT32 EFIAPI ReadUnaligned32 (CONST UINT32 *p) { UINT32 v; memcpy (&v, p, 4); return v; }
UINT64 EFIAPI ReadUnaligned64 (CONST UINT64 *p) { UINT64 v; memcpy (&v, p, 8); return v; }
UINT32 EFIAPI WriteUnaligned32 (UINT32 *p, UINT32 v) { memcpy (p, &v, 4); return v; }
UINTN EFIAPI __AsciiStrLen (CONST CHAR8 *s) { return strlen (s); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *a, CONST CHAR8 *b) { return strcmp (a, b); }
INTN EFIAPI AsciiStrnCmp (CONST CHAR8 *a, CONST CHAR8 *b, UINTN n) { return strncmp (a, b, n); }
INTN EFIAPI StrCmp (CONST CHAR16 *a, CONST CHAR16 *b) {
  while (*a && *a == *b) { a++; b++; }
  return (INTN)*a - *b;
}
INTN EFIAPI CompareMem (CONST VOID *a, CONST VOID *b, UINTN n) { return memcmp (a, b, n); }
VOID *EFIAPI CopyMem (VOID *a, CONST VOID *b, UINTN n) { return memmove (a, b, n); }
VOID *EFIAPI ZeroMem (VOID *p, UINTN n) { return memset (p, 0, n); }
BOOLEAN EFIAPI CompareGuid (CONST GUID *a, CONST GUID *b) { return memcmp (a, b, sizeof (*a)) == 0; }
VOID EFIAPI FreePool (VOID *p) { free (p); }
VOID *EFIAPI AllocatePool (UINTN n) { return malloc (n); }
UINTN EFIAPI __StrLen (CONST CHAR16 *s) {
  UINTN n = 0;
  while (s[n]) n++;
  return n;
}
RETURN_STATUS EFIAPI __StrnCpyS (CHAR16 *dest, UINTN capacity, CONST CHAR16 *src, UINTN count) {
  UINTN n = 0;
  while (n < count && src[n] && n + 1 < capacity) { dest[n] = src[n]; n++; }
  dest[n] = 0;
  return RETURN_SUCCESS;
}
RETURN_STATUS EFIAPI __StrnCatS (CHAR16 *dest, UINTN capacity, CONST CHAR16 *src, UINTN count) {
  UINTN n = __StrLen (dest);
  return __StrnCpyS (dest + n, capacity - n, src, count);
}
VOID *EFIAPI AllocateAlignedPages (UINTN pages, UINTN alignment) {
  VOID *p = NULL;
  if (posix_memalign (&p, alignment, EFI_PAGES_TO_SIZE (pages)) != 0) return NULL;
  return p;
}
VOID EFIAPI FreeAlignedPages (VOID *p, UINTN pages) { (void)pages; free (p); }
