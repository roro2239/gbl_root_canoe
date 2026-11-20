/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbBootOptions.h"

#include <IndustryStandard/PeImage.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

typedef struct {
  UINT8                    *Data;
  EFI_IMAGE_SECTION_HEADER *Sections;
  UINTN                     Count;
} SFB_BOOT_IMAGE;

STATIC
BOOLEAN
SfbBootImageInit (IN VOID *Buffer, IN UINTN Size, OUT SFB_BOOT_IMAGE *Image)
{
  UINT8 *Data = Buffer;
  UINTN Pe;
  UINTN Table;
  UINTN Index;
  UINTN Other;
  UINT16 OptionalSize;

  if (Data == NULL || Size < sizeof (EFI_IMAGE_DOS_HEADER)) {
    return FALSE;
  }
  Pe = ReadUnaligned16 ((UINT16 *)Data) == EFI_IMAGE_DOS_SIGNATURE
         ? ReadUnaligned32 ((UINT32 *)(Data + 0x3c)) : 0;
  if (Pe > Size - 24 ||
      ReadUnaligned32 ((UINT32 *)(Data + Pe)) != EFI_IMAGE_NT_SIGNATURE ||
      ReadUnaligned16 ((UINT16 *)(Data + Pe + 4)) != IMAGE_FILE_MACHINE_ARM64) {
    return FALSE;
  }
  Image->Count = ReadUnaligned16 ((UINT16 *)(Data + Pe + 6));
  OptionalSize = ReadUnaligned16 ((UINT16 *)(Data + Pe + 20));
  if (OptionalSize < sizeof (EFI_IMAGE_OPTIONAL_HEADER64) ||
      OptionalSize > Size - Pe - 24 || Image->Count == 0 || Image->Count > 96 ||
      ReadUnaligned16 ((UINT16 *)(Data + Pe + 24)) != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return FALSE;
  }
  Table = Pe + 24 + OptionalSize;
  if (Image->Count > (Size - Table) / sizeof (EFI_IMAGE_SECTION_HEADER) ||
      (Table & 3) != 0) {
    return FALSE;
  }
  Image->Data = Data;
  Image->Sections = (EFI_IMAGE_SECTION_HEADER *)(Data + Table);
  if ((ReadUnaligned64 ((UINT64 *)(Data + Pe + 48)) & 0xfff) != 0) {
    return FALSE;
  }
  for (Index = 0; Index < Image->Count; Index++) {
    EFI_IMAGE_SECTION_HEADER *Section = &Image->Sections[Index];
    UINT64 End = (UINT64)Section->VirtualAddress +
                 MAX (Section->Misc.VirtualSize, Section->SizeOfRawData);
    if (Section->PointerToRawData > Size ||
        Section->SizeOfRawData > Size - Section->PointerToRawData ||
        End > MAX_UINT32 ||
        (Section->SizeOfRawData != 0 &&
         Section->PointerToRawData < Table + Image->Count * sizeof (*Section))) {
      return FALSE;
    }
    for (Other = 0; Other < Index; Other++) {
      EFI_IMAGE_SECTION_HEADER *Previous = &Image->Sections[Other];
      UINT64 PreviousEnd = (UINT64)Previous->VirtualAddress +
                            MAX (Previous->Misc.VirtualSize, Previous->SizeOfRawData);
      if ((Section->VirtualAddress < PreviousEnd && Previous->VirtualAddress < End) ||
          (Section->SizeOfRawData != 0 && Previous->SizeOfRawData != 0 &&
           Section->PointerToRawData < (UINT64)Previous->PointerToRawData + Previous->SizeOfRawData &&
           Previous->PointerToRawData < (UINT64)Section->PointerToRawData + Section->SizeOfRawData)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

STATIC
BOOLEAN
SfbBootImageString (IN CONST SFB_BOOT_IMAGE *Image, IN UINTN Offset,
                    IN UINT32 Rva, IN CONST CHAR8 *Text)
{
  UINT32 Adrp = ReadUnaligned32 ((UINT32 *)(Image->Data + Offset));
  UINT32 Add = ReadUnaligned32 ((UINT32 *)(Image->Data + Offset + 4));
  INT64 Pages;
  INT64 Target;
  UINTN Index;
  UINTN Length = AsciiStrLen (Text) + 1;

  if ((Adrp & 0x9f000000) != 0x90000000 ||
      (Add & 0xffc00000) != 0x91000000 ||
      (Adrp & 31) == 31 || (Add & 31) != (Adrp & 31) ||
      ((Add >> 5) & 31) != (Adrp & 31)) {
    return FALSE;
  }
  Pages = ((Adrp >> 29) & 3) | ((Adrp >> 5) & 0x7ffff) << 2;
  if ((Pages & 0x100000) != 0) {
    Pages -= 0x200000;
  }
  Target = (INT64)(Rva & ~0xfffU) + Pages * 4096 + ((Add >> 10) & 0xfff);
  if (Target < 0 || Target > MAX_UINT32) {
    return FALSE;
  }
  for (Index = 0; Index < Image->Count; Index++) {
    EFI_IMAGE_SECTION_HEADER *Section = &Image->Sections[Index];
    UINT64 Delta;
    if ((UINT64)Target < Section->VirtualAddress) {
      continue;
    }
    Delta = (UINT64)Target - Section->VirtualAddress;
    if (Delta <= Section->SizeOfRawData && Length <= Section->SizeOfRawData - Delta) {
      return CompareMem (Image->Data + Section->PointerToRawData + (UINTN)Delta,
                          Text, Length) == 0;
    }
  }
  return FALSE;
}

STATIC
EFI_STATUS
SfbFindWarningPatch (IN CONST SFB_BOOT_IMAGE *Image, OUT UINTN *Candidate)
{
  UINTN Index;
  UINTN Anchors = 0;
  UINTN Candidates = 0;

  for (Index = 0; Index < Image->Count; Index++) {
    EFI_IMAGE_SECTION_HEADER *Section = &Image->Sections[Index];
    UINTN Offset;
    UINTN Start = Section->PointerToRawData;
    UINTN End = Start + Section->SizeOfRawData;
    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0 ||
        Section->SizeOfRawData < 16 || ((Start | Section->VirtualAddress) & 3) != 0) {
      continue;
    }
    for (Offset = Start; Offset <= End - 16; Offset += 4) {
      UINT32 Rva = Section->VirtualAddress + (UINT32)(Offset - Start);
      UINTN Distance;
      if (!SfbBootImageString (Image, Offset, Rva, "Orange State\n") ||
          !SfbBootImageString (Image, Offset + 8, Rva + 8,
                              "Your device has been unlocked and can't be trusted\n")) {
        continue;
      }
      Anchors++;
      for (Distance = 4; Distance <= 64 && Distance <= Offset - Start; Distance += 4) {
        UINTN Branch = Offset - Distance;
        UINT32 Instruction = ReadUnaligned32 ((UINT32 *)(Image->Data + Branch));
        INT32 Delta;
        INT64 Target;
        if (Instruction == 0xd503233f) {
          break;
        }
        /* 原始条件寄存器已被去黄字补丁改成 WZR，不猜测恢复寄存器。 */
        if ((Instruction & 0xff00001f) != 0x3400001f) {
          continue;
        }
        Delta = (INT32)((Instruction >> 5) & 0x7ffff);
        if ((Delta & 0x40000) != 0) {
          Delta -= 0x80000;
        }
        Target = (INT64)Branch + (INT64)Delta * 4;
        if (Target < (INT64)Offset + 16 || Target > (INT64)End - 4) {
          return EFI_UNSUPPORTED;
        }
        *Candidate = Branch;
        Candidates++;
      }
    }
  }
  return (Anchors == 1 && Candidates == 1) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
BOOLEAN
SfbBootImageCall (IN CONST SFB_BOOT_IMAGE *Image, IN UINTN Offset, IN UINT32 Rva)
{
  UINT32 Instruction = ReadUnaligned32 ((UINT32 *)(Image->Data + Offset));
  INT64 Delta = Instruction & 0x03ffffff;
  INT64 Target;
  UINTN Index;

  if ((Instruction & 0xfc000000) != 0x94000000) {
    return FALSE;
  }
  if ((Delta & 0x02000000) != 0) {
    Delta -= 0x04000000;
  }
  Target = (INT64)Rva + Delta * 4;
  for (Index = 0; Index < Image->Count; Index++) {
    EFI_IMAGE_SECTION_HEADER *Section = &Image->Sections[Index];
    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) != 0 &&
        Section->SizeOfRawData >= 4 && Target >= Section->VirtualAddress &&
        (UINT64)(Target - Section->VirtualAddress) <= Section->SizeOfRawData - 4) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC
EFI_STATUS
SfbFindFastbootPatch (IN CONST SFB_BOOT_IMAGE *Image, IN UINTN Size,
                      IN BOOLEAN Enabled, OUT UINTN *Candidate)
{
  STATIC CONST CHAR8 Message[] = "fastboot_unlock_verify error and reboot.";
  UINTN Index;
  BOOLEAN HasMessage = FALSE;
  BOOLEAN Found = FALSE;

  *Candidate = MAX_UINTN;
  for (Index = 0; Index + sizeof (Message) <= Size; Index++) {
    if (CompareMem (Image->Data + Index, Message, sizeof (Message)) == 0) {
      HasMessage = TRUE;
      break;
    }
  }
  if (!HasMessage) {
    DEBUG ((EFI_D_INFO, "SFB: no known official fastboot verification gate\n"));
    return EFI_SUCCESS;
  }
  for (Index = 0; Index < Image->Count; Index++) {
    EFI_IMAGE_SECTION_HEADER *Section = &Image->Sections[Index];
    UINTN Offset;
    UINTN Start = Section->PointerToRawData;
    UINTN End = Start + Section->SizeOfRawData;
    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0 ||
        Section->SizeOfRawData < 32 || ((Start | Section->VirtualAddress) & 3) != 0) {
      continue;
    }
    for (Offset = Start; Offset <= End - 32; Offset += 4) {
      UINT32 Rva = Section->VirtualAddress + (UINT32)(Offset - Start);
      UINT32 Branch;
      BOOLEAN Already;
      if (!SfbBootImageString (Image, Offset + 4, Rva + 4, Message)) {
        continue;
      }
      Branch = ReadUnaligned32 ((UINT32 *)(Image->Data + Offset));
      Already = (BOOLEAN)(Branch == 0x14000007);
      if (!Enabled && !Already) {
        continue;
      }
      if (Found || (!Already && Branch != 0x340000e0 && Branch != 0xb40000e0) ||
          (ReadUnaligned32 ((UINT32 *)(Image->Data + Offset + 4)) & 31) != 1 ||
          ReadUnaligned32 ((UINT32 *)(Image->Data + Offset + 12)) != 0x52b00000 ||
          ReadUnaligned32 ((UINT32 *)(Image->Data + Offset + 20)) != 0x528007c0 ||
          !SfbBootImageCall (Image, Offset + 16, Rva + 16) ||
          !SfbBootImageCall (Image, Offset + 24, Rva + 24)) {
        DEBUG ((EFI_D_ERROR, "SFB: unsupported official fastboot gate at 0x%Lx\n", (UINT64)Offset));
        return EFI_UNSUPPORTED;
      }
      if (Already && !Enabled) {
        /* 旧版已丢失 CBZ 的 W/X 位，必须重新生成镜像，不能猜测原始指令。 */
        DEBUG ((EFI_D_ERROR, "SFB: regenerate boot.efi with --defer-fastboot before disabling the fix\n"));
        return EFI_UNSUPPORTED;
      }
      Found = TRUE;
      if (!Already) {
        *Candidate = Offset;
      }
    }
  }
  return (!Enabled || Found) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

EFI_STATUS
SfbPatchBootImage (IN OUT VOID *Buffer, IN UINTN Size,
                   IN CONST SFB_BOOT_OPTIONS *Options)
{
  SFB_BOOT_IMAGE Image;
  EFI_STATUS Status;
  UINTN Warning = MAX_UINTN;
  UINTN Fastboot = MAX_UINTN;

  if (!SfbBootImageInit (Buffer, Size, &Image)) {
    return EFI_LOAD_ERROR;
  }
  if (Options->BootWarning) {
    Status = SfbFindWarningPatch (&Image, &Warning);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  Status = SfbFindFastbootPatch (&Image, Size, Options->FixFastboot, &Fastboot);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Warning != MAX_UINTN) {
    /* 调用方已确认真实 BL 解锁。CBNZ WZR 不跳转，执行原厂提示路径。 */
    UINT32 Instruction = ReadUnaligned32 ((UINT32 *)(Image.Data + Warning));
    WriteUnaligned32 ((UINT32 *)(Image.Data + Warning), Instruction | 0x01000000);
  }
  if (Fastboot != MAX_UINTN) {
    WriteUnaligned32 ((UINT32 *)(Image.Data + Fastboot), 0x14000007);
  }
  return EFI_SUCCESS;
}
