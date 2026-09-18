#include "arm64_inst/utils.h"
#include "patchs/core.h"
#include <stdlib.h>

int32_t patch_abl_gbl(char* buffer, int32_t size) {
    static const char target[]      = { 'e',0, 'f',0, 'i',0, 's',0, 'p',0 };
    static const char replacement[] = { 'n',0, 'u',0, 'l',0, 'l',0, 's',0 };
    int32_t target_len = sizeof(target);
    for (int32_t i = 0; i <= size - target_len; ++i) {
        if (memcmp(buffer + i, target, target_len) == 0) {
            memcpy(buffer + i, replacement, target_len);
            return 0;
        }
    }
    return -1;
}

static const int16_t Original[] = {
    -1, 0x00, 0x00, 0x34, 0x28, 0x00, 0x80, 0x52,
    0x06, 0x00, 0x00, 0x14, 0xE8, -1, 0x40, 0xF9,
    0x08, 0x01, 0x40, 0x39, 0x1F, 0x01, 0x00, 0x71,
    0xE8, 0x07, 0x9F, 0x1A, 0x08, 0x79, 0x1F, 0x53
};
static const int16_t Patched[] = {
    -1, -1, -1, -1, 0x08, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};

static int32_t patch_abl_bootstate_ex(char* buffer, int32_t size,
                                    int8_t* lock_register_num, int32_t* offset,
                                    bool apply) {
    int32_t pattern_len = sizeof(Original) / sizeof(int16_t);
    int32_t patched_count = 0;
    if (size < pattern_len) return 0;
    for (int32_t i = 0; i <= size - pattern_len; i += 4) {
        bool match = true;
        for (int32_t j = 0; j < pattern_len; ++j) {
            if (Original[j] != -1 && (uint8_t)buffer[i + j] != (uint8_t)Original[j]) {
                match = false; break;
            }
        }
        if (match) {
            *lock_register_num = (int8_t)((uint8_t)buffer[i] & 0x1F);
            *offset = i;
            #ifndef DISABLE_PATCH_3
            if (apply) {
                for (int32_t j = 0; j < pattern_len; ++j)
                    if (Patched[j] != -1) buffer[i + j] = (char)Patched[j];
            }
            #endif
            patched_count++;
            i += pattern_len - 4;
        }
    }
    return patched_count;
}
// track_forward_patch_strb callback example: patch STRB to STR, so we can use the same register for 64-bit value instead of 8-bit, which is more likely to be used in real code and easier to track back to source.
int32_t patch_strb_to_str_forward_callback(char* buffer, int32_t size, int32_t off, DecodedInst d, int32_t anchor_offset) {
    if (d.type == INST_STRB_IMM || d.type == INST_STRB_POST || d.type == INST_STRB_PRE) {
        if (off > anchor_offset) {
            StrbInfo si = decode_any_strb(d.raw);
            if (si.rn == 31) {
                printf("  0x%X: STRB W%d,[SP,#0x%X] ** SINK (after anchor0x%X) **\n",
                    off, si.rt, si.imm, anchor_offset);
            } else {
                printf("  0x%X: STRB W%d,[X%d,#0x%X] ** SINK (after anchor0x%X) **\n",
                    off, si.rt, si.rn, si.imm, anchor_offset);
            }
            printf("  Before: %02X %02X %02X %02X\n",
                       (uint8_t)buffer[off], (uint8_t)buffer[off+1],
                       (uint8_t)buffer[off+2], (uint8_t)buffer[off+3]);
            write_instr(buffer, off, strb_with_reg(d.raw, 31));

            printf("  After : %02X %02X %02X %02X (Rt -> WZR)\n",
                       (uint8_t)buffer[off], (uint8_t)buffer[off+1],
                       (uint8_t)buffer[off+2], (uint8_t)buffer[off+3]);
            return SUCCESS;
        }
    }
    return NEED_MORE;
}
static int32_t track_forward_patch_strb(char* buffer, int32_t size, int32_t ldrb_off,
                                      int8_t src_reg, int32_t anchor_off) {
    return track_forward(buffer, size, ldrb_off, src_reg, anchor_off, patch_strb_to_str_forward_callback);
}
//
int32_t source_callback(char* buffer, int32_t size, int32_t now_offset, int8_t current_target, int32_t anchor_offset) {
    printf("  Before: %02X %02X %02X %02X\n",
        (uint8_t)buffer[now_offset], (uint8_t)buffer[now_offset+1],
        (uint8_t)buffer[now_offset+2], (uint8_t)buffer[now_offset+3]);
    #ifndef DISABLE_PATCH_4
    write_instr(buffer, now_offset, encode_movz_w((uint8_t)current_target, 1));
    printf("  After : %02X %02X %02X %02X (MOV W%d, #1)\n",
        (uint8_t)buffer[now_offset], (uint8_t)buffer[now_offset+1],
        (uint8_t)buffer[now_offset+2], (uint8_t)buffer[now_offset+3],
        (int)current_target);
    #endif
    #ifndef DISABLE_PATCH_5
    int32_t fwd = track_forward_patch_strb(buffer, size, now_offset, current_target, anchor_offset);
    if (fwd != SUCCESS) {
        printf("Warning: sink STRB not found after anchor 0x%X\n", anchor_offset);
        return -1;
    }
    printf("Sink patched successfully.\n");
    #endif
    return 0;
}

int32_t patch_adrl_unlocked_to_locked(char* buffer, int32_t size, uint64_t load_base) {
    if (size < 24) return 0;
    int32_t patched = 0;

    for (int32_t i = 0; i <= size - 24; i += 4) {
        DecodedInst a0 = decode_at(buffer, i);
        DecodedInst a1 = decode_at(buffer, i + 4);
        DecodedInst b0 = decode_at(buffer, i + 8);
        DecodedInst b1 = decode_at(buffer, i + 12);

        if (a0.type != INST_ADRP || a1.type != INST_ADD_X_IMM) continue;
        if (a1.rt != a0.rt || a1.rn != a0.rt) continue;

        if (b0.type != INST_ADRP || b1.type != INST_ADD_X_IMM) continue;
        if (b1.rt != b0.rt || b1.rn != b0.rt) continue;


        uint8_t xa = a0.rt, xb = b0.rt;
        if (xa == xb) continue;

        int64_t off0 = calc_adrl_file_offset(buffer, i,      load_base);
        int64_t off1 = calc_adrl_file_offset(buffer, i + 8,  load_base);

        if (!str_at(buffer, size, off0, "unlocked")) continue;
        if (!str_at(buffer, size, off1, "locked"))   continue;
        bool match = false;
        for(int j=i+16; j<=i+40;j+=4){
            if (j + 7 >= size) break;
            DecodedInst c0 = decode_at(buffer, j);
            DecodedInst c1 = decode_at(buffer, j + 4);
            if(c0.type == INST_ADRP && c1.type == INST_ADD_X_IMM){
                int64_t offc = calc_adrl_file_offset(buffer, j, load_base);
                if(str_at(buffer, size, offc, "androidboot.vbmeta.device_state")){
                    match = true;
                    break;
                }
            }
        }
        if (!match) continue;
        printf("Found ADRL triple at 0x%X:\n", i);
        printf("  [0x%X] ADRP+ADD X%d -> file:0x%llX \"unlocked\"\n",
               i, xa, (unsigned long long)off0);
        printf("  [0x%X] ADRP+ADD X%d -> file:0x%llX \"locked\"\n",
               i+8, xb, (unsigned long long)off1);

        uint32_t new_adrp = adrp_with_rd(b0.raw, xa);
        uint32_t new_add  = add_with_reg(b1.raw, xa);

        printf("  Patch pair-0: ADRP %08X->%08X, ADD %08X->%08X\n",
               a0.raw, new_adrp, a1.raw, new_add);

        write_instr(buffer, i,     new_adrp);
        write_instr(buffer, i + 4, new_add);

        patched++;
        i += 20;
    }

    if (patched == 0)
        printf("ADRL triple not found\n");
    else
        printf("ADRL patch applied: %d location(s)\n", patched);

    return patched;
}

#include "patchs/oplus/warning.h"
#include "patchs/oplus/forceenablefastboot.h"
static bool patch_buffer_inplace(char* data, int32_t size, PatchMode mode) {
    /* 只读定位必须先于假回锁写入；也防止把已做假回锁的输入当作真实状态来源。 */
    int32_t offset = -1;
    int8_t lock_register_num = -1;
    int32_t num_patches = patch_abl_bootstate_ex(data, size, &lock_register_num, &offset, false);
    if (num_patches != 1) {
        printf("错误：原始启动状态锚点应唯一，实际找到 %d 处；请使用原始 ABL。\n", num_patches);
        return false;
    }
    printf("补丁模式：%s\n", mode == PATCH_MODE_NORMAL ? "normal（真实状态透传）" : "fake_locked（假回锁）");

    int32_t global_var_offset = -1;
    if (find_ldrB_instructio_reverse(data, size, offset, lock_register_num,
                                   &global_var_offset, empty_source_callback) != SUCCESS) {
        printf("错误：无法定位原始锁状态来源，停止生成产物。\n");
        return false;
    }

    /* 两种模式使用相同的 fastboot 验证绕过，不能依赖被伪装的锁状态。 */
    if (patch_fastboot(data, size) == FASTBOOT_PATCH_ERROR) return false;
    if (patch_abl_gbl(data, size) != 0)
        printf("Warning: Failed to patch ABL GBL\n");

    if (mode == PATCH_MODE_NORMAL) {
        if (!patch_warning(data, size, global_var_offset)) {
            printf("提示：未应用 OPlus 去黄字补丁，保留原警告显示。\n");
        }
        printf("真实状态模式：未修改状态字符串、锁状态读取及状态写回指令。\n");
        return true;
    }

    int32_t patched_adrl = patch_adrl_unlocked_to_locked(data, size, 0);
    if (patched_adrl != 1) {
        printf("错误：假回锁状态字符串应匹配唯一位置，实际为 %d，停止生成产物。\n", patched_adrl);
        return false;
    }
    patch_abl_bootstate_ex(data, size, &lock_register_num, &offset, true);
    printf("Anchor offset : 0x%X\n", offset);
    printf("Lock register : W%d\n", (int)lock_register_num);
    printf("Boot patches: %d\n", num_patches);

    if (find_ldrB_instructio_reverse(data, size, offset, lock_register_num, &global_var_offset, source_callback) != 0) {
        printf("错误：W%d 的假回锁状态读写链修补失败，停止生成产物。\n",
               (int)lock_register_num);
        return false;
    }
    printf("Global variable offset (for warning patch): 0x%X\n", global_var_offset);
    // ===================== 启用去黄字补丁 =====================


    //oplus
    if (!patch_warning(data, size, global_var_offset)) {
        printf("OPlus Warning: patch_warning failed\n");
    }

    // ==========================================================

    return 1;
}

bool PatchBufferEx(char* data, int32_t size, PatchMode mode) {
    if (data == NULL || size <= 0 ||
        (mode != PATCH_MODE_NORMAL && mode != PATCH_MODE_FAKE_LOCKED)) return false;
    char* pending = malloc((size_t)size);
    if (pending == NULL) {
        printf("错误：无法分配补丁缓冲区。\n");
        return false;
    }
    memcpy(pending, data, (size_t)size);
    bool result = patch_buffer_inplace(pending, size, mode);
    if (result) memcpy(data, pending, (size_t)size);
    free(pending);
    return result;
}

bool PatchBuffer(char* data, int32_t size) {
    return PatchBufferEx(data, size, PATCH_MODE_FAKE_LOCKED);
}
