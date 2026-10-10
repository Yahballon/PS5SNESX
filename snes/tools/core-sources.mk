# (AI-assisted) Upstream SNES balanced profile; objects are built out of tree.
CORE_DIR := .
STATIC_LINKING := 0
PROFILE := balanced
include Makefile.common
.PHONY: list
list:
	@printf '%s\n' $(SOURCES_CXX) $(SOURCES_C)
