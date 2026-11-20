/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef NULL
#include <Uefi.h>
#include <IndustryStandard/PeImage.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include "SuperFbBootOptions.h"

UINT16 EFIAPI ReadUnaligned16 (CONST UINT16 *Buffer) {
  UINT16 Value;
  memcpy (&Value, Buffer, sizeof (Value));
  return Value;
}

UINT32 EFIAPI ReadUnaligned32 (CONST UINT32 *Buffer) {
  UINT32 Value;
  memcpy (&Value, Buffer, sizeof (Value));
  return Value;
}

UINT64 EFIAPI ReadUnaligned64 (CONST UINT64 *Buffer) {
  UINT64 Value;
  memcpy (&Value, Buffer, sizeof (Value));
  return Value;
}

UINT32 EFIAPI WriteUnaligned32 (UINT32 *Buffer, UINT32 Value) {
  memcpy (Buffer, &Value, sizeof (Value));
  return Value;
}

UINTN EFIAPI __AsciiStrLen (CONST CHAR8 *String) {
  return strlen (String);
}

INTN EFIAPI CompareMem (CONST VOID *First, CONST VOID *Second, UINTN Size) {
  return memcmp (First, Second, Size);
}

static UINT8 Image[0xa00];
static UINT8 Original[sizeof (Image)];

static void instruction (UINTN Offset, UINT32 Value) {
  WriteUnaligned32 ((UINT32 *)(Image + Offset), Value);
}

static void gate (UINTN Offset, BOOLEAN Wide) {
  instruction (Offset, Wide ? 0xb40000e0 : 0x340000e0);
  instruction (Offset + 4, 0xf0000001); /* ADRP X1 -> RVA 0x4000 */
  instruction (Offset + 8, 0x91040021);
  instruction (Offset + 12, 0x52b00000);
  instruction (Offset + 16, 0x94000001);
  instruction (Offset + 20, 0x528007c0);
  instruction (Offset + 24, 0x94000001);
}

static void fixture (BOOLEAN Wide) {
  EFI_IMAGE_DOS_HEADER *Dos = (VOID *)Image;
  EFI_IMAGE_NT_HEADERS64 *Pe = (VOID *)(Image + 0x80);
  EFI_IMAGE_SECTION_HEADER *Sections = (VOID *)(Image + 0x188);

  memset (Image, 0, sizeof (Image));
  Dos->e_magic = EFI_IMAGE_DOS_SIGNATURE;
  Dos->e_lfanew = 0x80;
  Pe->Signature = EFI_IMAGE_NT_SIGNATURE;
  Pe->FileHeader.Machine = IMAGE_FILE_MACHINE_ARM64;
  Pe->FileHeader.NumberOfSections = 2;
  Pe->FileHeader.SizeOfOptionalHeader = sizeof (Pe->OptionalHeader);
  Pe->OptionalHeader.Magic = EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  Pe->OptionalHeader.ImageBase = 0x80000000;
  Sections[0].VirtualAddress = 0x1000;
  Sections[0].PointerToRawData = 0x400;
  Sections[0].SizeOfRawData = Sections[0].Misc.VirtualSize = 0x200;
  Sections[0].Characteristics = EFI_IMAGE_SCN_MEM_EXECUTE;
  Sections[1].VirtualAddress = 0x4000;
  Sections[1].PointerToRawData = 0x800;
  Sections[1].SizeOfRawData = Sections[1].Misc.VirtualSize = 0x200;
  strcpy ((char *)Image + 0x800, "Orange State\n");
  strcpy ((char *)Image + 0x820, "Your device has been unlocked and can't be trusted\n");
  strcpy ((char *)Image + 0x900, "fastboot_unlock_verify error and reboot.");
  gate (0x400, Wide);
  instruction (0x47c, 0xd503233f);
  instruction (0x480, 0x3400021f);
  instruction (0x484, 0xf0000000);
  instruction (0x488, 0x91000000);
  instruction (0x48c, 0xf0000001);
  instruction (0x490, 0x91008021);
}

int main (int Argc, char **Argv) {
  if (Argc > 1) {
    for (int Index = 1; Index < Argc; Index++) {
      FILE *File = fopen (Argv[Index], "rb");
      assert (File != NULL && fseek (File, 0, SEEK_END) == 0);
      long Length = ftell (File);
      assert (Length > 0 && fseek (File, 0, SEEK_SET) == 0);
      UINT8 *Source = malloc ((size_t)Length);
      UINT8 *Working = malloc ((size_t)Length);
      assert (Source != NULL && Working != NULL);
      assert (fread (Source, 1, (size_t)Length, File) == (size_t)Length);
      assert (fclose (File) == 0);
      for (int Warning = 0; Warning < 2; Warning++) {
        for (int Fastboot = 0; Fastboot < 2; Fastboot++) {
          SFB_BOOT_OPTIONS Options = { Warning, Fastboot, TRUE };
          memcpy (Working, Source, (size_t)Length);
          assert (SfbPatchBootImage (Working, (UINTN)Length, &Options) == EFI_SUCCESS);
          UINTN Changed = 0;
          UINTN Offset;
          for (Offset = 0; Offset + 4 <= (UINTN)Length; Offset += 4) {
            if (memcmp (Working + Offset, Source + Offset, 4) != 0) Changed++;
          }
          assert (Changed == (UINTN)(Warning + Fastboot));
          assert (memcmp (Working + Offset, Source + Offset, (size_t)Length - Offset) == 0);
        }
      }
      free (Working);
      free (Source);
      printf ("真实 loader 的四种开关组合通过：%s\n", Argv[Index]);
    }
    return 0;
  }
  for (int Wide = 0; Wide < 2; Wide++) {
    for (int Warning = 0; Warning < 2; Warning++) {
      for (int Fastboot = 0; Fastboot < 2; Fastboot++) {
        SFB_BOOT_OPTIONS Options = { Warning, Fastboot, TRUE };
        fixture (Wide);
        memcpy (Original, Image, sizeof (Image));
        if (Warning) WriteUnaligned32 ((UINT32 *)(Original + 0x480), 0x3500021f);
        if (Fastboot) WriteUnaligned32 ((UINT32 *)(Original + 0x400), 0x14000007);
        assert (SfbPatchBootImage (Image, sizeof (Image), &Options) == EFI_SUCCESS);
        assert (memcmp (Image, Original, sizeof (Image)) == 0);
      }
    }
  }
  SFB_BOOT_OPTIONS Options = { FALSE, TRUE, TRUE };
  fixture (FALSE);
  assert (SfbPatchBootImage (Image, sizeof (Image), &Options) == EFI_SUCCESS);
  memcpy (Original, Image, sizeof (Image));
  assert (SfbPatchBootImage (Image, sizeof (Image), &Options) == EFI_SUCCESS);
  assert (memcmp (Image, Original, sizeof (Image)) == 0);
  Options.FixFastboot = FALSE;
  assert (SfbPatchBootImage (Image, sizeof (Image), &Options) == EFI_UNSUPPORTED);
  assert (memcmp (Image, Original, sizeof (Image)) == 0);

  Options.BootWarning = Options.FixFastboot = TRUE;
  fixture (FALSE);
  gate (0x440, FALSE);
  memcpy (Original, Image, sizeof (Image));
  assert (SfbPatchBootImage (Image, sizeof (Image), &Options) == EFI_UNSUPPORTED);
  assert (memcmp (Image, Original, sizeof (Image)) == 0);
  fixture (FALSE);
  instruction (0x410, 0x94001000); /* 调用目标越过可执行段。 */
  memcpy (Original, Image, sizeof (Image));
  assert (SfbPatchBootImage (Image, sizeof (Image), &Options) == EFI_UNSUPPORTED);
  assert (memcmp (Image, Original, sizeof (Image)) == 0);
  fixture (FALSE);
  assert (SfbPatchBootImage (Image, 0x950, &Options) == EFI_LOAD_ERROR);
  puts ("启动开关组合、PE 地址映射、旧版兼容和失败不修改测试通过");
  return 0;
}
