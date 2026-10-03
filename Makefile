# Oboro: Moonlight (Sunshine / GameStream) client for the New 3DS with
# Kasumi's interface. Run from devkitPro MSYS or the Docker image:
#   tools/fetch-deps.sh && tools/build-deps.sh   (once)
#   make

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error Run this from devkitPro MSYS or set DEVKITARM.)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET := Oboro
BUILD := build
MOONLIGHT := third_party/moonlight-common-c
SOURCES := source \
           libgamestream \
           third_party/libuuid \
           $(MOONLIGHT)/src \
           $(MOONLIGHT)/enet \
           $(MOONLIGHT)/reedsolomon
INCLUDES := include \
            libgamestream \
            third_party/libuuid \
            $(MOONLIGHT)/src \
            $(MOONLIGHT)/enet/include \
            $(MOONLIGHT)/reedsolomon

APP_TITLE := Oboro
APP_DESCRIPTION := Stream your PC to the New 3DS
APP_AUTHOR := Oboro contributors

# Release version: GitHub releases are tagged v$(VERSION). The CIA's own
# version field carries MAJOR.MINOR.MICRO. Run `make clean` after changing it.
VERSION_MAJOR := 0
VERSION_MINOR := 1
VERSION_MICRO := 0
VERSION_SUFFIX := -alpha.1
VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)$(VERSION_SUFFIX)
APP_PRODUCT_CODE := CTR-P-OBRO
APP_UNIQUE_ID := 0xB0207
RSF := resources/app.rsf
BANNER_IMAGE := resources/banner.png
BANNER_AUDIO := resources/banner.wav

# Set these to the host binaries when building a CIA. The standard 3DSX build
# intentionally needs only devkitPro.
MAKEROM ?= makerom
BANNERTOOL ?= bannertool

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS += $(INCLUDE) -D__3DS__ -DUSE_MBEDTLS -DAPP_VERSION=\"$(VERSION)\"
# moonlight-common-c, enet, libgamestream and libuuid are built as Moonlight-N3DS
# builds them: unmodified upstream code that newer GCC is stricter about.
CFLAGS += -Wno-implicit-function-declaration -Wno-incompatible-pointer-types
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $(OUTPUT)).map
LIBDIRS := $(PORTLIBS) $(CTRULIB)
# libssl/libcrypto (pairing) and libexpat come from tools/build-deps.sh.
LIBS := -Wl,--start-group -lcurl -lssl -lcrypto -lexpat -ljansson -lopus -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm -Wl,--end-group
export LIBS

ifneq ($(BUILD),$(notdir $(CURDIR)))

ifeq ($(wildcard $(MOONLIGHT)/src/Limelight.h),)
$(error $(MOONLIGHT) is missing: run tools/fetch-deps.sh first)
endif

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export APP_ICON := $(CURDIR)/resources/icon.png
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES_SOURCES := $(CFILES:.c=.o)
export CACERT_OBJECT := $(CURDIR)/$(BUILD)/cacert_pem.o
# UI art: gfx/NAME.png -> NAME.t3x (tex3ds) -> linked read-only data exposing
# _binary_NAME_t3x_start/_end. Opaque art uses RGB565 to match the top screen.
GFX_NAMES := $(basename $(notdir $(wildcard gfx/*.png)))
export GFX_OBJECTS := $(foreach name,$(GFX_NAMES),$(CURDIR)/$(BUILD)/$(name).t3x.o)
export OFILES := $(OFILES_SOURCES) $(CACERT_OBJECT) $(GFX_OBJECTS)
export LD := $(CC)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS := --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: all clean cia version
all: $(BUILD) $(CACERT_OBJECT) $(GFX_OBJECTS)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

$(CACERT_OBJECT): romfs/cacert.pem | $(BUILD)
	@$(OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=.rodata.cacert,alloc,load,readonly,data,contents "$<" "$@"

GFX_FORMAT_hero := rgb565

$(BUILD)/%.t3x: gfx/%.png | $(BUILD)
	@$(DEVKITPRO)/tools/bin/tex3ds -f $(or $(GFX_FORMAT_$*),rgba8) -z auto -o "$@" "$<" > /dev/null

$(CURDIR)/$(BUILD)/%.t3x.o: $(BUILD)/%.t3x
	@cd $(BUILD) && $(OBJCOPY) -I binary -O elf32-littlearm -B arm 		--rename-section .data=.rodata.gfx,alloc,load,readonly,data,contents "$*.t3x" "$*.t3x.o"

clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).map $(TARGET).cia

cia: all
	@$(BANNERTOOL) makebanner -i "$(BANNER_IMAGE)" -a "$(BANNER_AUDIO)" -o "$(BUILD)/banner.bnr"
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(APP_ICON)" -f "visible,nosavebackups,new3ds" -o "$(BUILD)/icon.icn"
	@$(MAKEROM) -f cia -o "$(TARGET).cia" -target t -exefslogo -elf "$(TARGET).elf" -rsf "$(RSF)" -banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" -major $(VERSION_MAJOR) -minor $(VERSION_MINOR) -micro $(VERSION_MICRO)

version:
	@echo $(VERSION)

else

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)
$(OFILES_SOURCES):

-include $(DEPSDIR)/*.d

endif
