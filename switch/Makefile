.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET := saikou_switch
BUILD := build
SOURCES := source
DATA := data
INCLUDES := include
BOREALIS_PATH := borealis
ROMFS := romfs
OUT_SHADERS := shaders

APP_TITLE := Saikou Switch
APP_AUTHOR := SaikouTV Switch Port
APP_VERSION := 0.2.0
APP_DESCRIPTION := Native Switch Saikou UI prototype

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH)
CFLAGS += -D__SWITCH__ $(INCLUDE)
CXXFLAGS := $(CFLAGS) -std=c++1z -O2 -Wno-volatile -include optional
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# libcurl is the Switch-native HTTPS client; keep it before libnx for static link order.
LIBS := -lcurl -lz -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)

include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GLSLFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.glsl)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Explicitly pass the ROMFS directory to libnx's elf2nro packaging step.
# This is inherited by the recursive make running inside $(BUILD).
ifneq ($(strip $(ROMFS)),)
export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

# Keep every resource under romfs/ as an explicit dependency so libnx's
# NRO packaging step cannot consider a stale/incomplete ROMFS up to date.
ROMFS_TARGETS :=
ROMFS_FOLDERS :=
ROMFS_FILES := $(shell find $(ROMFS) -type f 2>/dev/null)
ROMFS_TARGETS += $(ROMFS_FILES)

ifneq ($(strip $(OUT_SHADERS)),)
ROMFS_SHADERS := $(ROMFS)/$(OUT_SHADERS)
ROMFS_TARGETS += $(patsubst %.glsl,$(ROMFS_SHADERS)/%.dksh,$(GLSLFILES))
ROMFS_FOLDERS += $(ROMFS_SHADERS)
endif

export ROMFS_DEPS := $(foreach file,$(ROMFS_TARGETS),$(CURDIR)/$(file))

.PHONY: all clean
all: $(ROMFS_TARGETS) | $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(strip $(OUT_SHADERS)),)
$(ROMFS_TARGETS): | $(ROMFS_FOLDERS)
$(ROMFS_FOLDERS):
	@mkdir -p $@

$(ROMFS_SHADERS)/%_vsh.dksh: %_vsh.glsl
	@uam -s vert -o $@ $<

$(ROMFS_SHADERS)/%_fsh.dksh: %_fsh.glsl
	@uam -s frag -o $@ $<

$(ROMFS_SHADERS)/%.dksh: %.glsl
	@uam -s comp -o $@ $<
endif

clean:
	@rm -fr $(BUILD) $(ROMFS_SHADERS) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_DEPS)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
