# openpak.nro — libnx + SDL2 + SDL2_ttf. Build with devkitA64:
#   podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make
.SUFFIXES:
# `make test` and `make clean` are host-side and must work without devkitPro.
ifeq ($(filter test clean,$(MAKECMDGOALS)),)
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set — build inside the devkitpro/devkita64 image)
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules
endif

TARGET   := openpak
BUILD    := build
SOURCES  := source
INCLUDES := source
APP_TITLE   := OpenPak
APP_AUTHOR  := OpenPak
APP_VERSION := 0.1.0

ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(DEFINES) \
           `sdl2-config --cflags` -I$(PORTLIBS)/include/SDL2 \
           -D__SWITCH__ $(INCLUDE)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# Let pkg-config resolve the SDL2/FreeType/HarfBuzz chain — hand-written link orders rot
# every time a portlib changes what it depends on.
PKGCONF := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
LIBS    := $(shell $(PKGCONF) --static --libs SDL2_ttf sdl2 2>/dev/null) -lnx -lm
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
# hosts_test.c is the host-side check; it must not go into the NRO.
CFILES   := $(filter-out hosts_test.c,$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
# Link with the C++ driver: these are C sources, but SDL2 drags in mesa (C++).
export LD := $(CXX)   # SDL2 pulls in mesa/libEGL, which is C++ and needs libstdc++
export APP_TITLE APP_AUTHOR APP_VERSION

.PHONY: all clean test
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).elf $(TARGET).nacp

# Runs on a PC, not the console: the toggle logic with a temp SD root.
test:
	@cc -o /tmp/openpak_hosts_test source/hosts.c source/hosts_test.c && /tmp/openpak_hosts_test

else
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
