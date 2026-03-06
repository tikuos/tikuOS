# ===========================================================================
#  TikuOS Memory Presentations — LaTeX Build Makefile
# ===========================================================================
#
#  Usage:
#    make              Build all presentation PDFs
#    make arena        Build arena allocator presentation
#    make persist      Build persistent store presentation
#    make view-arena   Build and open arena PDF
#    make view-persist Build and open persistent store PDF
#    make clean        Remove all build artifacts
#    make distclean    Remove artifacts + output PDFs
#    make help         Show this help
#
#  Engine override:
#    make ENGINE=lualatex
#    make ENGINE=xelatex
#
# ===========================================================================

# --- Configuration ---------------------------------------------------------
TEX_ARENA   := tikuos_memory_arena
TEX_PERSIST := tikuos_memory_persist
ENGINE      ?= pdflatex
VIEWER      ?= xdg-open
BUILD_DIR   := build

# Engine-specific flags
PDFLATEX_FLAGS := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)
LUALATEX_FLAGS := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)
XELATEX_FLAGS  := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)

# --- Targets ---------------------------------------------------------------
.PHONY: all arena persist view-arena view-persist clean distclean help

all: arena persist

arena: $(TEX_ARENA).pdf

persist: $(TEX_PERSIST).pdf

# --- Build rules -----------------------------------------------------------
%.pdf: %.tex
	@mkdir -p $(BUILD_DIR)
	@echo "==> Building $< with $(ENGINE) (pass 1/2)..."
	$(ENGINE) $($(shell echo $(ENGINE) | tr a-z A-Z)_FLAGS) $<
	@echo "==> Building $< with $(ENGINE) (pass 2/2)..."
	$(ENGINE) $($(shell echo $(ENGINE) | tr a-z A-Z)_FLAGS) $<
	@cp $(BUILD_DIR)/$@ .
	@echo "==> Output: $@"

# --- View -------------------------------------------------------------------
view-arena: $(TEX_ARENA).pdf
	@echo "==> Opening $(TEX_ARENA).pdf..."
	$(VIEWER) $(TEX_ARENA).pdf &

view-persist: $(TEX_PERSIST).pdf
	@echo "==> Opening $(TEX_PERSIST).pdf..."
	$(VIEWER) $(TEX_PERSIST).pdf &

# --- Clean ------------------------------------------------------------------
clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f *.aux *.log *.nav *.out *.snm *.toc *.vrb

distclean: clean
	rm -f $(TEX_ARENA).pdf $(TEX_PERSIST).pdf

# --- Help -------------------------------------------------------------------
help:
	@echo ""
	@echo "  TikuOS Memory Presentations Build System"
	@echo "  ========================================="
	@echo ""
	@echo "  make              Build all presentations"
	@echo "  make arena        Build arena allocator presentation"
	@echo "  make persist      Build persistent store presentation"
	@echo "  make view-arena   Build and open arena PDF"
	@echo "  make view-persist Build and open persistent store PDF"
	@echo "  make clean        Remove build artifacts"
	@echo "  make distclean    Remove artifacts + output PDFs"
	@echo "  make help         Show this help"
	@echo ""
	@echo "  Variables:"
	@echo "    ENGINE=pdflatex  LaTeX engine (pdflatex|lualatex|xelatex)"
	@echo "    VIEWER=xdg-open  PDF viewer command"
	@echo ""
