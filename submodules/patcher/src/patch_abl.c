#include "patchs/core.h"
//FILE
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* ==================== main ==================== */
static int read_file(const char *filename, char **data, int32_t *size) {
    FILE *file = fopen(filename, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long length = ftell(file);
    if (length <= 0 || length > INT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    *size = (int32_t)length;
    *data = malloc((size_t)*size);
    if (!*data) {
        fclose(file);
        return -1;
    }
    if (fread(*data, 1, (size_t)*size, file) != (size_t)*size || fclose(file) != 0) {
        free(*data);
        *data = NULL;
        return -1;
    }
    return 0;
}

int32_t main(int32_t argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        printf("用法：%s <输入文件> <输出文件> [normal|fake_locked]\n默认模式：fake_locked（假回锁）\n", argv[0]);
        return EXIT_FAILURE;
    }
    PatchMode mode = PATCH_MODE_FAKE_LOCKED;
    if (argc == 4) {
        if (strcmp(argv[3], "normal") == 0) mode = PATCH_MODE_NORMAL;
        else if (strcmp(argv[3], "fake_locked") != 0) {
            printf("错误：未知模式 %s，应为 normal 或 fake_locked。\n", argv[3]);
            return EXIT_FAILURE;
        }
    }
    char* data = NULL;
    int32_t size = 0;
    if (read_file(argv[1], &data, &size) != 0) {
        printf("Failed to read file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (!PatchBufferEx(data,size,mode))
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
