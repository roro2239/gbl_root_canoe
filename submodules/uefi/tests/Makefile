EDK2 := ../edk2
CFLAGS := -std=c11 -g -Wall -Wextra -Werror -fshort-wchar -DMDEPKG_NDEBUG \
	-DDISABLE_NEW_DEPRECATED_INTERFACES \
	-fsanitize=address,undefined -I$(EDK2)/MdePkg/Include \
	-I$(EDK2)/MdePkg/Include/X64

.PHONY: test-ui
test-ui:
	@set -e; test_dir=$$(mktemp -d); \
	trap 'rm -f "$$test_dir/test_banner_layout"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		-I$(EDK2)/AndroidToolsPkg/Include test_banner_layout.c \
		$(EDK2)/AndroidToolsPkg/Library/AndroidToolsUi/AndroidToolsUi.c \
		-Wl,--gc-sections -o "$$test_dir/test_banner_layout"; \
	"$$test_dir/test_banner_layout"
