# Thin wrapper around CMake presets.
#
# This file is named GNUmakefile on purpose: GNU make resolves makefiles in the
# order GNUmakefile > makefile > Makefile, so `make` always picks this wrapper
# even when CMake generates its own `Makefile` in the source tree (in-source
# builds). CMake never writes a GNUmakefile, so the two never collide.

CMAKE ?= cmake

# Default generator is Ninja. Override per-invocation with `make GEN=...` or via
# the environment (e.g. `CMAKE_GENERATOR=...`). CMake honors CMAKE_GENERATOR
# because the presets deliberately do not pin a generator.
GEN ?= Ninja
export CMAKE_GENERATOR := $(GEN)

PRESETS := debug release relwithdeb

.PHONY: all $(PRESETS) clean distclean

all: debug

$(PRESETS):
	$(CMAKE) --preset $@
	$(CMAKE) --build --preset $@
	@ln -sf build/$@/compile_commands.json compile_commands.json

# Run the configured build's clean target for every preset that exists.
clean:
	@for p in $(PRESETS); do \
	  if [ -d build/$$p ]; then $(CMAKE) --build --preset $$p --target clean; fi; \
	done

# Wipe all build artifacts.
distclean:
	rm -rf build compile_commands.json
