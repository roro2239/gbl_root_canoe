/* 使用实际提示补丁与存储实现，覆盖识别失败的原子性及旧记录位置兼容。 */
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbWarning.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbStore.c"

static UINT8 disk[8192];
static EFI_STATUS read_status, write_status, flush_status;

static EFI_STATUS EFIAPI read_blocks (EFI_BLOCK_IO_PROTOCOL *io, UINT32 id,
                                      EFI_LBA lba, UINTN size, VOID *buffer) {
  (void)id;
  assert (lba * io->Media->BlockSize + size <= sizeof (disk));
  if (!EFI_ERROR (read_status)) memcpy (buffer, disk + lba * io->Media->BlockSize, size);
  return read_status;
}
static EFI_STATUS EFIAPI write_blocks (EFI_BLOCK_IO_PROTOCOL *io, UINT32 id,
                                       EFI_LBA lba, UINTN size, VOID *buffer) {
  (void)id;
  assert (lba * io->Media->BlockSize + size <= sizeof (disk));
  if (!EFI_ERROR (write_status)) memcpy (disk + lba * io->Media->BlockSize, buffer, size);
  return write_status;
}
static EFI_STATUS EFIAPI flush_blocks (EFI_BLOCK_IO_PROTOCOL *io) {
  (void)io;
  return flush_status;
}

static void test_store (UINT32 block_size) {
  EFI_BLOCK_IO_MEDIA media = {0};
  EFI_BLOCK_IO_PROTOCOL io = {0};
  UINT8 before[sizeof (disk)];
  CHAR8 record[1024];
  BOOLEAN enabled;
  media.BlockSize = block_size;
  media.IoAlign = block_size;
  io.Media = &media;
  io.ReadBlocks = read_blocks;
  io.WriteBlocks = write_blocks;
  io.FlushBlocks = flush_blocks;
  mSfbStore = (SFB_STORE_LOCATION){&io, sizeof (disk) - SFB_STORE_BYTES, TRUE, FALSE};
  memset (disk, 0xff, sizeof (disk));
  strcpy ((char *)disk + sizeof (disk) - 2048, "SFB1|persist|\\efisp\\boot_normal.efi|Android");
  strcpy ((char *)disk + sizeof (disk) - 1024, "SFB1|custom|\\test.efi|Custom");
  memcpy (before, disk, sizeof (disk));
  assert (SfbLoadBootWarning (&enabled) == EFI_SUCCESS && !enabled);
  assert (SfbStoreRead (SFB_STORE_DEFAULT, record, sizeof (record)) == EFI_SUCCESS);
  assert (strcmp (record, (char *)before + sizeof (disk) - 2048) == 0);
  assert (SfbStoreRead (SFB_STORE_CUSTOM, record, sizeof (record)) == EFI_SUCCESS);
  assert (strcmp (record, (char *)before + sizeof (disk) - 1024) == 0);
  assert (SfbSaveBootWarning (TRUE) == EFI_SUCCESS);
  assert (memcmp (before, disk, sizeof (disk) - 3072) == 0);
  assert (memcmp (before + sizeof (disk) - 2048, disk + sizeof (disk) - 2048, 2048) == 0);
  /* 清除读取结果后再次加载，验证状态来自磁盘而非 UI 缓存。 */
  enabled = FALSE;
  assert (SfbLoadBootWarning (&enabled) == EFI_SUCCESS && enabled);
  assert (SfbStoreWrite (SFB_STORE_DEFAULT, "SFB1|new|\\boot.efi|Android") == EFI_SUCCESS);
  assert (SfbLoadBootWarning (&enabled) == EFI_SUCCESS && enabled);
  assert (SfbSaveBootWarning (FALSE) == EFI_SUCCESS);
  assert (SfbLoadBootWarning (&enabled) == EFI_SUCCESS && !enabled);
  assert (SfbStoreWrite (SFB_STORE_WARNING, "SFBW1|invalid") == EFI_SUCCESS);
  assert (SfbLoadBootWarning (&enabled) == EFI_VOLUME_CORRUPTED && !enabled);
  assert (SfbSaveBootWarning (FALSE) == EFI_SUCCESS);
  media.ReadOnly = TRUE;
  assert (SfbSaveBootWarning (TRUE) == EFI_WRITE_PROTECTED);
  media.ReadOnly = FALSE;
  write_status = EFI_DEVICE_ERROR;
  assert (SfbSaveBootWarning (TRUE) == EFI_DEVICE_ERROR);
  write_status = EFI_SUCCESS;
  assert (SfbLoadBootWarning (&enabled) == EFI_SUCCESS && !enabled);
  flush_status = EFI_DEVICE_ERROR;
  assert (SfbSaveBootWarning (TRUE) == EFI_DEVICE_ERROR);
  flush_status = EFI_SUCCESS;
  read_status = EFI_DEVICE_ERROR;
  assert (SfbLoadBootWarning (&enabled) == EFI_DEVICE_ERROR && !enabled);
  read_status = EFI_SUCCESS;
}

static void put32 (UINT8 *p, UINT32 n) { memcpy (p, &n, 4); }

/* 合成镜像故意让 RVA 与文件偏移不同，并采用非零 ImageBase。 */
static void fixture (UINT8 *data) {
  EFI_IMAGE_NT_HEADERS64 *pe = (VOID *)data;
  EFI_IMAGE_SECTION_HEADER *section = (VOID *)(data + sizeof (*pe));
  memset (data, 0, 4096);
  pe->Signature = EFI_IMAGE_NT_SIGNATURE;
  pe->FileHeader.Machine = IMAGE_FILE_MACHINE_ARM64;
  pe->FileHeader.NumberOfSections = 2;
  pe->FileHeader.SizeOfOptionalHeader = sizeof (pe->OptionalHeader);
  pe->OptionalHeader.Magic = EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  pe->OptionalHeader.ImageBase = 0x10000000;
  section[0].PointerToRawData = 0x200;
  section[0].SizeOfRawData = section[0].Misc.VirtualSize = 0x400;
  section[0].VirtualAddress = 0x2000;
  section[0].Characteristics = EFI_IMAGE_SCN_MEM_EXECUTE;
  section[1].PointerToRawData = 0x800;
  section[1].SizeOfRawData = section[1].Misc.VirtualSize = 0x200;
  section[1].VirtualAddress = 0x4000;
  put32 (data + 0x220, 0xd503233f); /* PACIASP */
  put32 (data + 0x230, 0x3400039f); /* CBZ WZR, +0x70 */
  put32 (data + 0x240, 0xd0000000); /* ADRP X0, +2 pages */
  put32 (data + 0x244, 0x91000000); /* ADD X0, X0, #0 */
  put32 (data + 0x248, 0xd0000001); /* ADRP X1, +2 pages */
  put32 (data + 0x24c, 0x91008021); /* ADD X1, X1, #0x20 */
  strcpy ((char *)data + 0x800, "Orange State\n");
  strcpy ((char *)data + 0x820, "Your device has been unlocked and can't be trusted\n");
}

static void rejected (UINT8 *data, UINTN size) {
  UINT8 before[4096];
  memcpy (before, data, sizeof (before));
  assert (EFI_ERROR (SfbEnableBootWarning (data, size)));
  assert (memcmp (before, data, sizeof (before)) == 0);
}

static void test_patch (void) {
  UINT8 data[4096], expected[4096];
  UINTN n;
  fixture (data);
  memcpy (expected, data, sizeof (data));
  put32 (expected + 0x230, 0x3500039f);
  assert (SfbEnableBootWarning (data, sizeof (data)) == EFI_SUCCESS);
  assert (memcmp (data, expected, sizeof (data)) == 0);
  rejected (data, sizeof (data)); /* 不把已修改或未知输入再当成隐藏版本。 */
  fixture (data);
  for (n = 0; n < 0xa00; n++) rejected (data, n);
  assert (SfbEnableBootWarning (NULL, sizeof (data)) == EFI_LOAD_ERROR);
  fixture (data); put32 (data + 0x230, 0x34000380); rejected (data, sizeof (data));
  fixture (data); put32 (data + 0x234, 0x3400037f); rejected (data, sizeof (data));
  fixture (data); put32 (data + 0x238, 0xd503233f); rejected (data, sizeof (data));
  fixture (data); put32 (data + 0x230, 0x347fffff); rejected (data, sizeof (data));
  fixture (data); put32 (data + 0x230, 0x3400001f); rejected (data, sizeof (data));
  fixture (data); data[0x800] = 'X'; rejected (data, sizeof (data));
  fixture (data); put32 (data + 0x244, 0x91000020); rejected (data, sizeof (data));
  fixture (data);
  memcpy (data + 0x300, data + 0x240, 16); /* 两处提示锚点必须拒绝。 */
  rejected (data, sizeof (data));
  fixture (data);
  EFI_IMAGE_SECTION_HEADER *section = (VOID *)(data + sizeof (EFI_IMAGE_NT_HEADERS64));
  section[1].PointerToRawData = 0x400; rejected (data, sizeof (data));
  fixture (data); section[1].VirtualAddress = 0x2000; rejected (data, sizeof (data));
  fixture (data); section[0].Characteristics = 0; rejected (data, sizeof (data));
  /* 大量截断和随机损坏输入在 ASan/UBSan 下不得越界或部分写入。 */
  for (n = 0; n < 2000; n++) {
    fixture (data);
    data[rand () % sizeof (data)] ^= (UINT8)(1 + rand () % 255);
    memcpy (expected, data, sizeof (data));
    if (EFI_ERROR (SfbEnableBootWarning (data, sizeof (data)))) {
      assert (memcmp (data, expected, sizeof (data)) == 0);
    }
  }
}

int main (int argc, char **argv) {
  if (argc == 1) {
    test_store (512);
    test_store (4096);
    test_patch ();
    puts ("默认关闭、旧记录兼容、持久化、错误路径与指令边界测试通过");
    return 0;
  }
  assert (argc == 3);
  FILE *input = fopen (argv[1], "rb");
  assert (input && fseek (input, 0, SEEK_END) == 0);
  long size = ftell (input);
  assert (size > 0 && fseek (input, 0, SEEK_SET) == 0);
  VOID *data = malloc ((size_t)size);
  assert (data && fread (data, 1, (size_t)size, input) == (size_t)size);
  assert (fclose (input) == 0);
  EFI_STATUS status = SfbEnableBootWarning (data, (UINTN)size);
  if (EFI_ERROR (status)) {
    free (data);
    fprintf (stderr, "提示恢复失败：0x%lx\n", (unsigned long)status);
    return 1;
  }
  FILE *output = fopen (argv[2], "wb");
  assert (output && fwrite (data, 1, (size_t)size, output) == (size_t)size);
  assert (fclose (output) == 0);
  free (data);
  return 0;
}
