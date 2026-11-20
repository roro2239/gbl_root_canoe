#include "patchs/core.h"
//FILE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* ==================== main ==================== */
int32_t read_file(const char* filename, char** data, int32_t* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    long length = ftell(file);
    if (length <= 0 || length > INT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    *size = (int32_t)length;
    *data = (char*)malloc(*size);
    if (!*data) { fclose(file); return -1; }
    if ((int32_t)fread(*data, 1, *size, file) != *size) {
        free(*data); fclose(file); return -1;
    }
    if (fclose(file) != 0) { free(*data); return -1; }
    return 0;
}
int32_t main(int32_t argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        printf("用法：%s <输入文件> <输出文件> [normal|fake_locked] [--defer-fastboot]\n", argv[0]);
        return EXIT_FAILURE;
    }
    PatchMode mode = PATCH_MODE_FAKE_LOCKED;
    bool mode_selected = false;
    bool fix_fastboot = true;
    for (int32_t i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--defer-fastboot") == 0 && fix_fastboot) {
            fix_fastboot = false;
        } else if (!mode_selected && (strcmp(argv[i], "normal") == 0 ||
                                     strcmp(argv[i], "fake_locked") == 0)) {
            mode = strcmp(argv[i], "normal") == 0 ? PATCH_MODE_NORMAL : PATCH_MODE_FAKE_LOCKED;
            mode_selected = true;
        } else {
            printf("错误：无效或重复的参数 %s。\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    char* data = NULL;
    int32_t size = 0;
    if (read_file(argv[1], &data, &size) != 0) {
        printf("Failed to read file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (!PatchBufferWithMode(data, size, mode, fix_fastboot))
    {
        printf("Patching failed\n");
        free(data);
        return EXIT_FAILURE;
    }
    FILE* out = fopen(argv[2], "wb");
    if (!out) {
        printf("Failed to open output: %s\n", argv[2]);
        free(data);
        return EXIT_FAILURE;
    }
    if (fwrite(data, 1, size, out) != size) {
        printf("Failed to write output\n");
        fclose(out);
        free(data);
        return EXIT_FAILURE;
    }
    if (fclose(out) != 0) {
        printf("错误：输出文件关闭失败。\n");
        free(data);
        return EXIT_FAILURE;
    }
    free(data);
    printf("Saved to %s\n", argv[2]);
    return EXIT_SUCCESS;
}
