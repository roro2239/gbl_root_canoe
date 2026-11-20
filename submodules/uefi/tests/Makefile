EDK2 := ../edk2
LOADER := $(EDK2)/QcomModulePkg/Application/LinuxLoader
CFLAGS := -std=c11 -g -Wall -Wextra -Werror -fshort-wchar -DMDEPKG_NDEBUG \
	-DDISABLE_NEW_DEPRECATED_INTERFACES \
	-fsanitize=address,undefined -I$(EDK2)/MdePkg/Include \
	-I$(EDK2)/MdePkg/Include/X64 -I$(LOADER)

.PHONY: test test-ui test-settings test-menu
test:
	@set -e; test_dir=$$(mktemp -d); \
	trap 'rm -f "$$test_dir/test_boot_options"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) test_boot_options.c $(LOADER)/SuperFbBootPatches.c -o "$$test_dir/test_boot_options"; \
	"$$test_dir/test_boot_options" $(IMAGES)

test-ui:
	@set -e; test_dir=$$(mktemp -d); \
	trap 'rm -f "$$test_dir/test_banner_layout"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		-I$(EDK2)/AndroidToolsPkg/Include test_banner_layout.c \
		$(EDK2)/AndroidToolsPkg/Library/AndroidToolsUi/AndroidToolsUi.c \
		$(EDK2)/AndroidToolsPkg/Library/AndroidToolsUi/MenuScreen.c \
		-Wl,--gc-sections,--wrap=MenuScreenOpen,--wrap=MenuScreenClear,--wrap=MenuScreenLine \
		-o "$$test_dir/test_banner_layout"; \
	"$$test_dir/test_banner_layout"

test-settings:
	@set -e; test_dir=$$(mktemp -d); \
	trap 'rm -f "$$test_dir/test_boot_settings"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Wno-unused-parameter \
		-I$(EDK2)/AndroidToolsPkg/Include -I$(EDK2)/QcomModulePkg/Include \
		test_boot_settings.c $(LOADER)/SuperFbEntries.c \
		-Wl,--gc-sections -o "$$test_dir/test_boot_settings"; \
	"$$test_dir/test_boot_settings"

test-menu:
	@set -e; test_dir=$$(mktemp -d); \
	trap 'rm -f "$$test_dir/test_boot_menu"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Wno-unused-parameter \
		-I$(EDK2)/AndroidToolsPkg/Include -I$(EDK2)/QcomModulePkg/Include \
		test_boot_menu.c $(LOADER)/SuperFbTitle.c $(LOADER)/SuperFbEntryList.c \
		-Wl,--gc-sections -o "$$test_dir/test_boot_menu"; \
	"$$test_dir/test_boot_menu"
