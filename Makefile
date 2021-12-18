# __  __          _
# \ \/ /   _ _ __(_)___
#  \  / | | | '__| / __|
#  /  \ |_| | |  | \__ \
# /_/\_\__, |_|  |_|___/
#      |___/
#
# Compiles the kernel source code located in the kernel folder.

# Designed by Keeton Feavel & Micah Switzer
# Copyright the Xyris Contributors (c) 2019

export PROJ_NAME = xyris

# Makefile flags
# prevent make from showing "entering/leaving directory" messages
export MAKEFLAGS  += --no-print-directory
# Xyris Repo
export REPO_URL   := "https://git.io/JWjEx"
# Xyris Version
export GIT_COMMIT := "$(shell git describe --abbrev=8 --dirty --always --tags)"
export VER_MAJOR  := "0"
export VER_MINOR  := "4"
export VER_PATCH  := "0"
export VER_NAME   := "Phoenix"
# Build output types
export MODE       ?= debug
export IMGTYPE    ?= img

# ******************************
# * Compiler Output Formatting *
# ******************************

export COLOR_COM  := \033[0;34m
export COLOR_OK   := \033[0;32m
export COLOR_INFO := \033[0;93m
export COLOR_NONE := \033[m

# *******************
# * i686 Toolchains *
# *******************

# Compilers/Assemblers/Linkers
# CXX is also used as linker per
# the OSDev wiki recommendations
export NASM    := $(shell which nasm)
export AR      := $(shell which i686-elf-ar)
export CC      := $(shell which i686-elf-gcc)
export CXX     := $(shell which i686-elf-g++)
export OBJCP   := $(shell which i686-elf-objcopy)

# *****************************
# * Source Code & Directories *
# *****************************

# Directories & files
export KERNEL         := kernel
export ROOT           := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
export KERNEL_DIR     := $(ROOT)/Kernel
export BUILD_DIR      := $(ROOT)/Build
export LIBRARY_DIR    := $(ROOT)/Libraries
export TESTS_DIR      := $(ROOT)/Tests
export PRODUCTS_DIR   := $(ROOT)/Distribution
export THIRDPARTY_DIR := $(ROOT)/Thirdparty
# Products
export ISO            := $(PROJ_NAME).iso
export IMG            := $(PROJ_NAME).img
export SYMBOLS        := $(KERNEL).sym
export BOOTIMG        := $(PROJ_NAME).$(IMGTYPE)

# Libraries
export LIB_DIRS := $(shell find $(LIBRARY_DIR) -mindepth 1 -maxdepth 1 -type d)

# *******************
# * Toolchain Flags *
# *******************

# Warning flags
export WARNINGS :=          \
	-Wall                   \
	-Werror                 \
	-Wextra                 \
	-Wundef                 \
	-Winline                \
	-Wshadow                \
	-Wformat=2              \
	-Wcast-align            \
	-Wno-long-long          \
	-Wpointer-arith         \
	-Wwrite-strings         \
	-Wredundant-decls       \
	-Wdouble-promotion      \
	-Wno-unused-function    \
	-Wmissing-declarations
# C only warnings
export CWARNINGS :=         \
	-Wnested-externs        \
	-Wstrict-prototypes     \
	-Wmissing-prototypes    \
# C flags
export CFLAGS :=            \
	-nostdlib               \
	-nodefaultlibs          \
	-ffreestanding          \
	-fstack-protector-all   \
	-fno-builtin            \
	-fno-omit-frame-pointer \
	-mno-avx                \
	-mno-sse                \
	${XYRIS_CFLAGS}         \
	${WARNINGS}
# C++ flags
export CXXFLAGS :=          \
	-std=c++20              \
	-fpermissive            \
	-fno-rtti               \
	-fno-exceptions         \
	-fno-use-cxa-atexit     \
	${XYRIS_CXXFLAGS}       \
# C / C++ pre-processor flags
export CPPFLAGS :=                \
	${XYRIS_CPPFLAGS}             \
	-D REPO_URL=\"$(REPO_URL)\"   \
	-D COMMIT=\"$(GIT_COMMIT)\"   \
	-D VER_MAJOR=\"$(VER_MAJOR)\" \
	-D VER_MINOR=\"$(VER_MINOR)\" \
	-D VER_PATCH=\"$(VER_PATCH)\" \
	-D VER_NAME=\"$(VER_NAME)\"
# Assembler flags
export ASFLAGS :=       \
	${XYRIS_ASFLAGS}
# Linker flags
export LDFLAGS :=       \
	${XYRIS_LDFLAGS}    \
	-nostdlib           \
	-lgcc               \
	-L.

# ************************
# * Kernel Build Targets *
# ************************

# Debug
ifeq ($(MODE), debug)
	CPPFLAGS += -DDEBUG
	CXXFLAGS += -ggdb3
	CFLAGS += -ggdb3
endif
# Release
ifeq ($(MODE), release)
	CPPFLAGS += -DRELEASE
	CXXFLAGS += -O3
	CFLAGS += -O3
endif

# Kernel (Linked With Libraries)
.PHONY: $(PRODUCTS_DIR)/$(MODE)/$(KERNEL)
$(PRODUCTS_DIR)/$(MODE)/$(KERNEL):
	@printf "$(COLOR_INFO)Making Libs ($(MODE))$(COLOR_NONE)\n"
	@for dir in $(LIB_DIRS); do        \
        $(MAKE) -C $$dir $(PROJ_NAME); \
    done
	@printf "$(COLOR_INFO)Making Kernel ($(MODE))$(COLOR_NONE)\n"
	@$(MAKE) -C $(KERNEL_DIR) $(KERNEL)
	@printf "$(COLOR_INFO)Done!$(COLOR_NONE)\n"

# Hacky way to build all targets. This target cannot
# be the first one otherwise it's a circular dependency.
all:
	@$(MAKE) MODE=debug
	@$(MAKE) MODE=release

# *********************
# * Kernel Unit Tests *
# *********************

.PHONY: unit-test
unit-test:
	@$(MAKE) -C $(TESTS_DIR) $@
	@$(RM) -r $(BUILD_DIR)/$@
	@$(RM) $(TESTS_DIR)/report.xml

# ********************************
# * Kernel Distribution Creation *
# ********************************

# Create a bootable image (either img or iso)
.PHONY: dist
dist: $(PRODUCTS_DIR)/$(MODE)/$(BOOTIMG)

# Create a bootable IMG
$(PRODUCTS_DIR)/$(MODE)/$(IMG): $(PRODUCTS_DIR)/$(MODE)/$(KERNEL) $(THIRDPARTY_DIR)/limine/limine-install-linux-x86_32 $(THIRDPARTY_DIR)/limine/limine.sys
	@printf "$(COLOR_INFO)Making Limine boot image ($(MODE))$(COLOR_NONE)\n"
	@rm -f $@
	@dd if=/dev/zero bs=1M count=0 seek=2 of=$@ 2> /dev/null
	@parted -s $@ mklabel msdos
	@parted -s $@ mkpart primary 1 100%
	@parted -s $@ set 1 boot on
	@echfs-utils -m -p0 $@ quick-format 32768
	@echfs-utils -m -p0 $@ import $(KERNEL_DIR)/Arch/i686/Bootloader/limine.cfg limine.cfg
	@echfs-utils -m -p0 $@ import $(THIRDPARTY_DIR)/limine/limine.sys limine.sys
	@echfs-utils -m -p0 $@ import $(PRODUCTS_DIR)/$(MODE)/$(KERNEL) kernel
	$(THIRDPARTY_DIR)/limine/limine-install-linux-x86_32 $@

# *************************
# * Virtual Machine Flags *
# *************************

# QEMU flags
QEMU_FLAGS =        \
    -m 4G           \
    -rtc clock=host \
    -vga std        \
    -serial stdio
QEMU_ARCH = x86_64
# Virtualbox flags
VM_NAME = $(PROJ_NAME)-box
VBOX_VM_FILE = $(PRODUCTS_DIR)/$(VM_NAME)/$(VM_NAME).vbox
# VM executable locations
VBOX = $(shell which VBoxManage)
QEMU = $(shell which qemu-system-$(QEMU_ARCH))

# ***************************
# * Virtual Machine Testing *
# ***************************

# Run Xyris in QEMU
.PHONY: run
run:
	$(QEMU)                                      \
	-drive file=$(PRODUCTS_DIR)/$(MODE)/$(BOOTIMG),\
	index=0,media=disk,format=raw \
	$(QEMU_FLAGS) \
	-monitor telnet:127.0.0.1:1234,server,nowait

# Open the connection to qemu and load our kernel-object file with symbols
.PHONY: run-debug
run-debug:
	# Start QEMU with debugger
	$(QEMU)   \
	-S -s      \
	-drive file=$(PRODUCTS_DIR)/$(MODE)/$(BOOTIMG),\
	index=0,media=disk,format=raw \
	$(QEMU_FLAGS) > /dev/null &

# Create Virtualbox VM
.PHONY: vbox-create
vbox-create: $(PRODUCTS_DIR)/$(MODE)/$(ISO)
	$(VBOX) createvm --register --name $(VM_NAME) --basefolder $(shell pwd)/$(PRODUCTS_DIR)
	$(VBOX) modifyvm $(VM_NAME)                 \
	--memory 256 --ioapic on --cpus 2 --vram 16 \
	--graphicscontroller vboxvga --boot1 disk   \
	--audiocontroller sb16 --uart1 0x3f8 4      \
	--uartmode1 file $(shell pwd)/com1.txt
	$(VBOX) storagectl $(VM_NAME) --name "DiskDrive" --add ide --bootable on
	$(VBOX) storageattach $(VM_NAME) --storagectl "DiskDrive" --port 1 --device 1 --type dvddrive --medium $(PRODUCTS_DIR)/$(ISO)

.PHONY: vbox-create
vbox: vbox-create
	$(VBOX) startvm --putenv --debug $(VM_NAME)

# ****************************
# * Documentation Generation *
# ****************************

.PHONY: docs
docs:
	@$(MAKE) -C Documentation

# ********************
# * Cleaning Targets *
# ********************

# Clear out objects and BIN
.PHONY: clean
clean:
	@printf "$(COLOR_INFO)Cleaning products...$(COLOR_NONE)\n"
	@$(RM) -r $(PRODUCTS_DIR)
	@printf "$(COLOR_INFO)Cleaning objects...$(COLOR_NONE)\n"
	@$(RM) -r $(BUILD_DIR)
	@printf "$(COLOR_INFO)Cleaning libraries...$(COLOR_NONE)\n"
	@for dir in $(LIB_DIRS); do \
	    printf " -   " &&       \
        $(MAKE) -C $$dir clean; \
    done
	@printf "$(COLOR_OK)Cleaning complete.$(COLOR_NONE)\n"

.PHONY: clean-tests
clean-tests:
	@$(MAKE) -C $(TESTS) clean

.PHONY: clean-vm
clean-vm:
	@echo Removing VM
	@$(VBOX) unregistervm $(VM_NAME) --delete
