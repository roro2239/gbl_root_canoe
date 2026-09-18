#include "patchs/oplus/forceenablefastboot.h"
#include "arm64_inst/utils.h"

/* 基于 ditvelo/gbl_root_canoe 的 OPlus 验证分支补丁，增加完整识别后再写入的检查。 */
static const char verify_error[] = "fastboot_unlock_verify error and reboot.";

static bool valid_call(const char* buffer, int32_t size, int32_t offset) {
    uint32_t raw = read_instr(buffer, offset);
    if ((raw & 0xfc000000u) != 0x94000000u) return false;
    int64_t imm = raw & 0x03ffffffu;
    if (imm & 0x02000000) imm -= 0x04000000;
    int64_t target = offset + imm * 4;
    return target >= 0 && target <= size - 4;
}

FastbootPatchResult patch_fastboot(char* buffer, int32_t size) {
    if (buffer == NULL || size <= 0) return FASTBOOT_PATCH_ERROR;

    bool has_message = false;
    for (int32_t i = 0; i <= size - (int32_t)sizeof(verify_error); ++i) {
        if (memcmp(buffer + i, verify_error, sizeof(verify_error)) == 0) {
            has_message = true;
            break;
        }
    }
    if (!has_message) {
        printf("[fastboot:not_found] 未发现已知 OPlus 验证标识，保持原有 fastboot 路径；不代表已验证可用。\n");
        return FASTBOOT_PATCH_NOT_FOUND;
    }

    int32_t candidate = -1;
    bool already = false;
    for (int32_t i = 0; i <= size - 12; i += 4) {
        int64_t message = calc_adrl_file_offset(buffer, i + 4, 0);
        if (!str_at(buffer, size, message, verify_error)) continue;

        /* 已验证的错误块：字符串参数、日志级别、日志调用、重启原因、重启调用。
         * 只接受跳过整个错误块的分支；未知布局不能仅凭字符串猜测修改。 */
        DecodedInst branch = decode_at(buffer, i);
        bool is_b = (branch.raw & 0xfc000000u) == 0x14000000u;
        int64_t displacement = branch.simm;
        if (is_b) {
            int64_t imm = branch.raw & 0x03ffffffu;
            if (imm & 0x02000000) imm -= 0x04000000;
            displacement = imm * 4;
        }
        if (candidate >= 0 || i > size - 32 || displacement != 28 ||
            (!is_b && ((branch.type != INST_CBZ_W && branch.type != INST_CBZ_X) || branch.rt != 0)) ||
            decode_at(buffer, i + 4).rt != 1 ||
            read_instr(buffer, i + 12) != 0x52b00000u ||
            read_instr(buffer, i + 20) != 0x528007c0u ||
            !valid_call(buffer, size, i + 16) || !valid_call(buffer, size, i + 24)) {
            printf("[fastboot:error] 0x%X 的验证分支不唯一或布局不受支持，停止生成产物。\n", i);
            return FASTBOOT_PATCH_ERROR;
        }
        candidate = i;
        already = is_b;
    }
    if (candidate < 0) {
        printf("[fastboot:error] 发现验证标识，但未能安全定位分支，停止生成产物。\n");
        return FASTBOOT_PATCH_ERROR;
    }
    if (already) {
        printf("[fastboot:already_applied] 0x%X 已跳过 OPlus 验证，保持不变。\n", candidate);
        return FASTBOOT_PATCH_ALREADY_APPLIED;
    }
    write_instr(buffer, candidate, change_to_b(read_instr(buffer, candidate)));
    printf("[fastboot:applied] 已将 0x%X 改为无条件跳转至 0x%X，绕过 OPlus fastboot 验证。\n",
           candidate, candidate + 28);
    return FASTBOOT_PATCH_APPLIED;
}
