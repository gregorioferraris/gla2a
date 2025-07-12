# --- General Project Settings ---
# Compiler (GCC/G++)
CXX = g++
CC = gcc # For C files like glad.c

# Standard C++ version (Dear ImGui requires C++11 or newer)
CXXSTANDARD = c++11
CSTANDARD = c99 # For glad.c

# List of all plugin bundles in this project
PLUGIN_BUNDLES = gla2a.lv2 gla3a.lv2 gua76.lv2

# LV2 Install Path (for `make deploy`)
LV2_INSTALL_PATH := ~/.lv2

# --- Common Compiler Flags ---
COMMON_CXXFLAGS = -fPIC -Wall -O2 -std=$(CXXSTANDARD)
COMMON_CFLAGS = -fPIC -Wall -O2 -std=$(CSTANDARD)

# Include Paths common to all plugins (e.g., LV2 headers)
LV2_HEADERS = $(shell pkg-config --cflags lv2)

# GUI specific includes (ImGui, GLAD, GLFW)
# These paths are relative to the *individual plugin bundle* directory.
# We'll prepend $(dir) in the specific rules.
IMGUI_INC = -Iimgui -Iglad
GLFW_INC = $(shell pkg-config --cflags glfw3)

# All GUI includes
GUI_INCLUDES = $(LV2_HEADERS) $(IMGUI_INC) $(GLFW_INC)

# Common Linker Flags for shared libraries
COMMON_LIBS = -shared -lm -ldl -lrt

# GUI specific libraries
GLFW_LIBS = $(shell pkg-config --libs glfw3) -lGL

# --- Targets ---
.PHONY: all clean deploy $(PLUGIN_BUNDLES)

# Default target: build all plugins
all: $(PLUGIN_BUNDLES)

# Rule for building each individual plugin bundle
$(PLUGIN_BUNDLES):
	$(MAKE) -C $@ all

# --- Individual Plugin Bundle Rules (Delegated to Sub-Makefiles) ---
# This Makefile acts as a master. Each plugin bundle will have its own simpler Makefile
# inside its directory (e.g., gla2a.lv2/Makefile).

# Dummy target to prevent 'make -C' from complaining if no sub-Makefile exists yet
.PHONY: $(PLUGIN_BUNDLES)

# --- Clean Target ---
clean:
	@echo "Cleaning all plugin bundles..."
	@for bundle in $(PLUGIN_BUNDLES); do \
		if [ -d "$$bundle" ]; then \
			$(MAKE) -C "$$bundle" clean || true; \
		fi \
	done
	@echo "Cleaning complete."

# --- Deploy Target ---
deploy: all
	@echo "Deploying all plugin bundles to $(LV2_INSTALL_PATH)..."
	@mkdir -p $(LV2_INSTALL_PATH)
	@for bundle in $(PLUGIN_BUNDLES); do \
		if [ -d "$$bundle" ]; then \
			cp -r "$$bundle" $(LV2_INSTALL_PATH)/; \
			echo "Deployed $$bundle"; \
		fi \
	done
	@echo "Deployment complete."
	@echo "You might need to refresh your DAW's plugin list."
