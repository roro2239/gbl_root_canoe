#include <assert.h>
#include <string.h>
#include "arm64_inst/utils.h"
#include "patchs/core.h"
#include "patchs/oplus/forceenablefastboot.h"

static char data[512], original[512];

static void gate(int offset, uint32_t branch) {
    write_instr(data, offset, branch);
    write_instr(data, offset + 4, 0x90000001); /* ADRP X1, page 0 */
    write_instr(data, offset + 8, 0x91040021); /* ADD X1, X1, #256 */
    write_instr(data, offset + 12, 0x52b00000);
    write_instr(data, offset + 16, 0x94000001);
    write_instr(data, offset + 20, 0x528007c0);
    write_instr(data, offset + 24, 0x94000001);
}

static void fixture(uint32_t branch) {
    memset(data, 0, sizeof(data));
    strcpy(data + 256, "fastboot_unlock_verify error and reboot.");
    gate(0, branch);
}

static void unchanged(FastbootPatchResult expected) {
    memcpy(original, data, sizeof(data));
    assert(patch_fastboot(data, sizeof(data)) == expected);
    assert(memcmp(original, data, sizeof(data)) == 0);
}

int main(void) {
    for (int wide = 0; wide < 2; ++wide) {
        fixture(wide ? 0xb40000e0 : 0x340000e0);
        memcpy(original, data, sizeof(data));
        write_instr(original, 0, 0x14000007);
        assert(patch_fastboot(data, sizeof(data)) == FASTBOOT_PATCH_APPLIED);
        assert(memcmp(original, data, sizeof(data)) == 0);
        unchanged(FASTBOOT_PATCH_ALREADY_APPLIED);
    }
    memset(data, 0, sizeof(data));
    unchanged(FASTBOOT_PATCH_NOT_FOUND);
    fixture(0x350000e0); /* CBNZ 不能被误识别。 */
    unchanged(FASTBOOT_PATCH_ERROR);
    fixture(0x340000c0); /* 跳转目标落在错误块内。 */
    unchanged(FASTBOOT_PATCH_ERROR);
    fixture(0x340000e0);
    write_instr(data, 16, 0x94001000);
    unchanged(FASTBOOT_PATCH_ERROR);
    fixture(0x340000e0);
    gate(64, 0x340000e0);
    unchanged(FASTBOOT_PATCH_ERROR);
    fixture(0x340000e0);
    write_instr(data, 8, 0x91080021); /* 字符串地址越界。 */
    unchanged(FASTBOOT_PATCH_ERROR);
    assert(!str_at(data, sizeof(data), 0x100000100LL, "fastboot_unlock_verify error and reboot."));
    assert(!str_at("lockedX", 8, 0, "locked"));
    assert(patch_fastboot(NULL, 0) == FASTBOOT_PATCH_ERROR);
    for (int size = 1; size < 40; ++size) {
        assert(patch_fastboot(data, size) == FASTBOOT_PATCH_NOT_FOUND);
        memcpy(original, data, sizeof(data));
        assert(!PatchBufferEx(data, size, PATCH_MODE_NORMAL));
        assert(memcmp(data, original, sizeof(data)) == 0);
    }
    fixture(0x340000e0);
    memcpy(original, data, sizeof(data));
    assert(!PatchBufferEx(data, sizeof(data), PATCH_MODE_FAKE_LOCKED));
    assert(!PatchBufferEx(data, sizeof(data), (PatchMode)42));
    assert(memcmp(data, original, sizeof(data)) == 0);
    /* fastboot 已在工作副本中修补，但假回锁字符串缺失：整体必须回滚。 */
    static const unsigned char anchor[] = {
        0x00, 0x00, 0x00, 0x34, 0x28, 0x00, 0x80, 0x52,
        0x06, 0x00, 0x00, 0x14, 0xe8, 0x00, 0x40, 0xf9,
        0x08, 0x01, 0x40, 0x39, 0x1f, 0x01, 0x00, 0x71,
        0xe8, 0x07, 0x9f, 0x1a, 0x08, 0x79, 0x1f, 0x53
    };
    write_instr(data, 60, 0x39400020); /* LDRB W0, [X1] */
    memcpy(data + 64, anchor, sizeof(anchor));
    memcpy(original, data, sizeof(data));
    assert(!PatchBufferEx(data, sizeof(data), PATCH_MODE_FAKE_LOCKED));
    assert(memcmp(data, original, sizeof(data)) == 0);
    assert(PatchBufferEx(data, sizeof(data), PATCH_MODE_NORMAL));
    write_instr(original, 0, 0x14000007);
    assert(memcmp(data, original, sizeof(data)) == 0);
    puts("分支识别、幂等、拒绝与边界测试通过");
    return 0;
}
