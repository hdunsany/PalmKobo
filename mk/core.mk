# Appended to CloudpilotEmu's own Makefiles (make -f Makefile -f core.mk) so that we
# reuse their source lists, include paths and compile rules verbatim, but build a
# headless static library instead of the SDL front end.
#
# Invoked from the top-level palmkobo Makefile; not meant to be run by hand.

PALMKOBO_CORE_LIB ?= libcpcore.a

# Core emulator + the two native helpers we reuse (util.cpp for ROM/session
# loading, assert_native/stacktrace for EmAssert support).
PALMKOBO_CORE_CXX = $(SOURCE_CXX) native/util.cpp emulator/assert_native.cpp emulator/stacktrace.cpp
PALMKOBO_CORE_OBJS = $(SOURCE_C:%.c=$(BUILDDIR_NATIVE)/%.o) $(PALMKOBO_CORE_CXX:%.cpp=$(BUILDDIR_NATIVE)/%.o)

$(PALMKOBO_CORE_LIB): $(PALMKOBO_CORE_OBJS)
	$(AR_NATIVE) cr $@ $^
	$(RANLIB_NATIVE) $@

palmkobo-core: $(PALMKOBO_CORE_LIB)

# Generic library of a module's shared sources (SOURCE_C + SOURCE_CXX), plus any
# extra files. Used for ../common (skipping Cli.cpp, which needs readline),
# ../vfs (FatFs) and ../fstools (mkfs for new SD card images).
PALMKOBO_LIB ?= libpalmkobo-part.a
PALMKOBO_EXTRA_CXX ?=
PALMKOBO_LIB_OBJS = $(SOURCE_C:%.c=$(BUILDDIR_NATIVE)/%.o) $(SOURCE_CXX:%.cpp=$(BUILDDIR_NATIVE)/%.o) \
	$(PALMKOBO_EXTRA_CXX:%.cpp=$(BUILDDIR_NATIVE)/%.o)

$(PALMKOBO_LIB): $(PALMKOBO_LIB_OBJS)
	$(AR_NATIVE) cr $@ $^
	$(RANLIB_NATIVE) $@

palmkobo-lib: $(PALMKOBO_LIB)

# Print the include path so the front end can compile against core headers.
palmkobo-print-include:
	@echo $(INCLUDE)

.PHONY: palmkobo-core palmkobo-lib palmkobo-print-include
