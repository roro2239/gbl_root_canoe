/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbWarning.h"

#include <IndustryStandard/PeImage.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

/* 只解析有文件数据的 PE 段，绝不把 RVA 直接当文件偏移使用。 */
typedef struct {
  UINT8                    *Data;
  EFI_IMAGE_SECTION_HEADER *Sections;
  UINTN                     Count;
} SFB_WARNING_IMAGE;

STATIC
BOOLEAN
SfbWarningImage (IN VOID *Buffer, IN UINTN Size, OUT SFB_WARNING_IMAGE *Image)
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
SfbWarningString (IN CONST SFB_WARNING_IMAGE *Image, IN UINTN Offset,
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
  /* ImageBase 已按页对齐，计算相对地址时可消去，避免高地址有符号溢出。 */
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

EFI_STATUS
SfbEnableBootWarning (IN OUT VOID *Buffer, IN UINTN Size)
{
  SFB_WARNING_IMAGE Image;
  UINTN Index;
  UINTN Anchors = 0;
  UINTN Candidates = 0;
  UINTN Candidate = 0;
  UINT32 Instruction;

  if (!SfbWarningImage (Buffer, Size, &Image)) {
    return EFI_LOAD_ERROR;
  }
  for (Index = 0; Index < Image.Count; Index++) {
    EFI_IMAGE_SECTION_HEADER *Section = &Image.Sections[Index];
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
      if (!SfbWarningString (&Image, Offset, Rva, "Orange State\n") ||
          !SfbWarningString (&Image, Offset + 8, Rva + 8,
                             "Your device has been unlocked and can't be trusted\n")) {
        continue;
      }
      Anchors++;
      for (Distance = 4; Distance <= 64 && Distance <= Offset - Start; Distance += 4) {
        UINTN Branch = Offset - Distance;
        INT32 Delta;
        INT64 Target;
        Instruction = ReadUnaligned32 ((UINT32 *)(Image.Data + Branch));
        if (Instruction == 0xd503233f) { /* PACIASP：不跨越函数边界。 */
          break;
        }
        /* patch_warning 仅改 Rt 为 WZR；原条件寄存器已丢失，不能猜测恢复。 */
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
        Candidate = Branch;
        Candidates++;
      }
    }
  }
  if (Anchors != 1 || Candidates != 1) {
    return EFI_UNSUPPORTED;
  }
  /* CBNZ WZR 永不跳转，从而执行原厂提示；调用方已确认真实 BL 为解锁。 */
  Instruction = ReadUnaligned32 ((UINT32 *)(Image.Data + Candidate)) | 0x01000000;
  WriteUnaligned32 ((UINT32 *)(Image.Data + Candidate), Instruction);
  return EFI_SUCCESS;
}
