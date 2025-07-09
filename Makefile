
.PHONY : all clean sdpti

default :sdpti src.build
install :src.install
BUILDDIR ?= $(abspath ./build)
ABSBUILDDIR := $(abspath $(BUILDDIR))
TARGET := src

DISABLE_SDPTI_HOOK ?= ON
SDPTI_HOOKLIB_DIR := $(abspath $(PWD))/sdpti_hooklib
SDPTI_HOOKLIB_LINK =
SDPTI_HOOKLIB =
SDPTI_FLAGS =

sdpti:
ifeq ($(DISABLE_SDPTI_HOOK), OFF)
	@mkdir -p $(SDPTI_HOOKLIB_DIR)/build && cd $(SDPTI_HOOKLIB_DIR)/build && cmake $(SDPTI_HOOKLIB_DIR)/build/../ && $(MAKE)
	$(eval SDPTI_HOOKLIB := -Wl,--whole-archive -lsdpti-hook-tccl -Wl,--no-whole-archive)
	$(eval SDPTI_HOOKLIB_LINK := -L$(SDPTI_HOOKLIB_DIR)/build/hook_tccl)
	$(eval SDPTI_FLAGS := -include $(SDPTI_HOOKLIB_DIR)/hook_tccl/generated/sdpti_hook_tccl_macro.h)
endif

export SDPTI_FLAGS
export DISABLE_SDPTI_HOOK
export SDPTI_HOOKLIB_LINK
export SDPTI_HOOKLIB
export SDPTI_HOOKLIB_DIR

clean : $(TARGET:%=%.clean)

src.%: sdpti
	$(MAKE) -C src $* BUILDDIR=${ABSBUILDDIR}
