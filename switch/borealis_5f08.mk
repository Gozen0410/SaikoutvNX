# Compatibility bridge for the newer Borealis 5f08 revision.
# That revision is CMake-first and no longer ships library/borealis.mk,
# while the Saikou Switch target still uses libnx's recursive Make flow.

BOREALIS_COMPAT_ROOT := $(BOREALIS_PATH)/library

LIBS := -ldeko3d -lm $(LIBS)

# Build the Switch Pulsar sources directly, matching Borealis' CMake target.
include $(TOPDIR)/$(BOREALIS_COMPAT_ROOT)/lib/extern/switch-libpulsar/deps.mk

# The newer Borealis layout keeps its implementation under library/lib.
# Keep these paths relative to the top-level Switch directory so the parent
# Makefile can export an absolute VPATH for the recursive build directory.
BOREALIS_SOURCE_ROOTS := \
	lib/core \
	lib/views \
	lib/platforms/switch \
	lib/extern/glad \
	lib/extern/libretro-common/compat \
	lib/extern/libretro-common/encodings \
	lib/extern/libretro-common/features \
	lib/extern/nanovg \
	lib/extern/yoga/yoga \
	lib/extern/tinyxml2 \
	lib/extern/fmt/src \
	lib/extern/switch-libpulsar/src/archive \
	lib/extern/switch-libpulsar/src/bfgrp \
	lib/extern/switch-libpulsar/src/bfsar \
	lib/extern/switch-libpulsar/src/bfwar \
	lib/extern/switch-libpulsar/src/bfwav \
	lib/extern/switch-libpulsar/src/bfwsd \
	lib/extern/switch-libpulsar/src/player

BOREALIS_SOURCE_DIRS := $(foreach root,$(BOREALIS_SOURCE_ROOTS),$(shell find $(BOREALIS_COMPAT_ROOT)/$(root) -type d 2>/dev/null))
SOURCES := $(SOURCES) $(BOREALIS_SOURCE_DIRS)

INCLUDES := $(INCLUDES) \
	$(BOREALIS_COMPAT_ROOT)/include \
	$(BOREALIS_COMPAT_ROOT)/include/borealis/extern \
	$(BOREALIS_COMPAT_ROOT)/include/borealis/extern/nanovg \
	$(BOREALIS_COMPAT_ROOT)/include/borealis/extern/tinyxml2 \
	$(BOREALIS_COMPAT_ROOT)/lib/extern/fmt/include \
	$(BOREALIS_COMPAT_ROOT)/lib/extern/tweeny/include \
	$(BOREALIS_COMPAT_ROOT)/lib/extern/yoga \
	$(BOREALIS_COMPAT_ROOT)/lib/extern/yoga/yoga \
	$(BOREALIS_COMPAT_ROOT)/lib/extern/switch-libpulsar/include

CFLAGS := $(CFLAGS) -DHAVE_LIBNX -DSWITCH -D__SWITCH__ -DSTBI_NO_THREAD_LOCALS
CXXFLAGS := $(CXXFLAGS) -DHAVE_LIBNX -DSWITCH -D__SWITCH__ -DSTBI_NO_THREAD_LOCALS -DYG_ENABLE_EVENTS -DBRLS_RESOURCES="\"romfs:/\"" -fdata-sections -Wno-volatile
