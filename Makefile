# openpak.nro — libnx + SDL2 + SDL2_ttf. Build with devkitA64:
#   podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make
.SUFFIXES:
# `make test` and `make clean` are host-side and must work without devkitPro.
ifeq ($(filter test clean,$(MAKECMDGOALS)),)
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set — build inside the devkitpro/devkita64 image)
endif
TOPDIR ?= $(CURDIR)
# Must be set before the include: switch_rules falls back to libnx's placeholder icon
# otherwise, and hbmenu would list this with the generic homebrew logo.
APP_ICON := $(TOPDIR)/icon.jpg
include $(DEVKITPRO)/libnx/switch_rules
endif

# UI=sdl (styled, needs title takeover) or UI=console (text, loads in applet mode).
UI       ?= sdl
TARGET   := openpak$(if $(filter console,$(UI)),-console,)
BUILD    := build
SOURCES  := source
INCLUDES := source
APP_TITLE   := OpenPak
APP_AUTHOR  := OpenPak
APP_VERSION := 0.3.7
# romfs carries the CA the console's browser must trust; there is no way to fetch it before
# the console trusts us.
ROMFS    := romfs

PKGCONF := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(DEFINES) -DOPENPAK_VERSION=\"$(APP_VERSION)\" \
           $(shell $(PKGCONF) --cflags freetype2 2>/dev/null) -D__SWITCH__ $(INCLUDE)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# Let pkg-config resolve the SDL2/FreeType/HarfBuzz chain — hand-written link orders rot
# every time a portlib changes what it depends on.
# FreeType only: text is rasterised into the framebuffer, so there is no SDL/EGL/mesa here and
# the NRO loads in applet mode.
LIBS    := $(if $(filter console,$(UI)),,$(shell $(PKGCONF) --static --libs freetype2 2>/dev/null)) -lcurl -lz -ljson-c -lnx -lm
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
# hosts_test.c is the host-side check; it must not go into the NRO.
# One UI per build; hosts_test.c is the host-side check and never goes into an NRO.
UI_SKIP  := hosts_test.c system_test.c installtrust_test.c report_test.c $(if $(filter console,$(UI)),main.c gfx.c text.c,main_console.c)
CFILES   := $(filter-out $(UI_SKIP),$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
# Link with the C++ driver: these are C sources, but SDL2 drags in mesa (C++).
export LD := $(CC)
# elf2nro packs nothing unless it is told to: without these the NRO has no title, no icon in
# hbmenu's list, and no romfs — which silently leaves out the CA the browser needs.
export NROFLAGS := --nacp=$(CURDIR)/$(TARGET).nacp --icon=$(APP_ICON) --romfsdir=$(CURDIR)/$(ROMFS)
export APP_TITLE APP_AUTHOR APP_VERSION APP_ICON
export ROMFS
export UI

.PHONY: all clean test
all: romfs/ca.der $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

romfs/ca.der: romfs/ca.pem
	openssl x509 -in $< -outform DER -out $@

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).elf $(TARGET).nacp

# Runs on a PC, not the console: the toggle logic with a temp SD root.
test:
	@cc -DOPENPAK_HOST_TEST -o /tmp/openpak_hosts_test source/hosts.c source/policy.c source/ca.c source/hosts_test.c \
		$(shell pkg-config --cflags --libs json-c) && /tmp/openpak_hosts_test
	@cc -DOPENPAK_HOST_TEST -o /tmp/openpak_it_test source/installtrust.c source/installtrust_test.c && /tmp/openpak_it_test
	@cc -Wall -Wextra -o /tmp/openpak_report_test source/report.c source/report_test.c \
		$(shell pkg-config --cflags --libs json-c) && /tmp/openpak_report_test
	@python3 tools/test-patches.py
	@python3 tools/test-system.py

else
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(shell find $(TOPDIR)/romfs -type f)
$(OUTPUT).nacp: $(TOPDIR)/Makefile
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
