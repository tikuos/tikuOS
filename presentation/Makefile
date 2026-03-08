# ===========================================================================
#  TikuOS Presentations — Unified Build System
# ===========================================================================
#
#  Directory layout:
#    src/<topic>/*.tex    LaTeX source files
#    pdf/                 Compiled PDF output
#    build/               Intermediate LaTeX artifacts
#
#  Usage:
#    make                 Build all presentations (default: pdflatex)
#    make ENGINE=lualatex Build all with lualatex
#    make ENGINE=xelatex  Build all with xelatex
#    make list            List discovered presentations
#    make clean           Remove build artifacts (keep PDFs)
#    make distclean       Remove artifacts + output PDFs
#    make help            Show this help
#
# ===========================================================================

# --- Configuration ---------------------------------------------------------
ENGINE    ?= pdflatex
VIEWER    ?= xdg-open
BUILD_DIR := build
PDF_DIR   := pdf
SRC_DIR   := src

# --- Auto-discover all .tex sources ---------------------------------------
TEX_FILES := $(sort $(wildcard $(SRC_DIR)/**/*.tex) $(wildcard $(SRC_DIR)/*/*.tex))
PDF_FILES := $(patsubst $(SRC_DIR)/%.tex,$(PDF_DIR)/%.pdf,$(TEX_FILES))

# Flatten: pdf/memory/tikuos_memory_arena.pdf -> pdf/tikuos_memory_arena.pdf
FLAT_PDFS := $(foreach p,$(PDF_FILES),$(PDF_DIR)/$(notdir $(p)))

# Engine-specific flags
LATEX_FLAGS := -interaction=nonstopmode -halt-on-error

# --- Phony targets ---------------------------------------------------------
.PHONY: all clean distclean list help

# --- Default: build everything ---------------------------------------------
all: $(FLAT_PDFS)
	@echo ""
	@echo "=== All presentations built ==="
	@echo "  PDFs in $(PDF_DIR)/:"
	@ls -1 $(PDF_DIR)/*.pdf 2>/dev/null | sed 's/^/    /'
	@echo ""

# --- Build rule: src/<topic>/foo.tex -> pdf/foo.pdf ------------------------
# We need a second-expansion so that each flat PDF can find its source .tex
# by searching through all discovered TEX_FILES.
.SECONDEXPANSION:

$(PDF_DIR)/%.pdf: $$(filter %/%.tex,$$(TEX_FILES))
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(PDF_DIR)
	@echo "==> Building $< -> $@ (pass 1/2)..."
	@$(ENGINE) $(LATEX_FLAGS) -output-directory=$(BUILD_DIR) $<
	@echo "==> Building $< -> $@ (pass 2/2)..."
	@$(ENGINE) $(LATEX_FLAGS) -output-directory=$(BUILD_DIR) $<
	@cp $(BUILD_DIR)/$(notdir $(basename $<)).pdf $@
	@echo "==> Output: $@"

# --- Fallback: explicit rules for each tex file ---------------------------
# .SECONDEXPANSION with filter can be fragile; generate explicit rules too.
define MAKE_PDF_RULE
$(PDF_DIR)/$(notdir $(patsubst %.tex,%.pdf,$(1))): $(1)
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(PDF_DIR)
	@echo "==> Building $$< -> $$@ (pass 1/2)..."
	@$$(ENGINE) $$(LATEX_FLAGS) -output-directory=$$(BUILD_DIR) $$<
	@echo "==> Building $$< -> $$@ (pass 2/2)..."
	@$$(ENGINE) $$(LATEX_FLAGS) -output-directory=$$(BUILD_DIR) $$<
	@cp $$(BUILD_DIR)/$$(notdir $$(basename $$<)).pdf $$@
	@echo "==> Output: $$@"
endef

$(foreach t,$(TEX_FILES),$(eval $(call MAKE_PDF_RULE,$(t))))

# --- Clean ------------------------------------------------------------------
clean:
	@echo "==> Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)

distclean: clean
	@echo "==> Cleaning PDFs..."
	@rm -f $(PDF_DIR)/*.pdf

# --- List -------------------------------------------------------------------
list:
	@echo ""
	@echo "  TikuOS Presentations"
	@echo "  ===================="
	@echo ""
	@echo "  Source files ($(SRC_DIR)/):"
	@$(foreach t,$(TEX_FILES),echo "    $(t)";)
	@echo ""
	@echo "  Output PDFs ($(PDF_DIR)/):"
	@$(foreach p,$(FLAT_PDFS),echo "    $(p)";)
	@echo ""

# --- Help -------------------------------------------------------------------
help:
	@echo ""
	@echo "  TikuOS Presentations — Unified Build System"
	@echo "  ============================================"
	@echo ""
	@echo "  Layout:"
	@echo "    src/<topic>/*.tex    LaTeX source files"
	@echo "    pdf/*.pdf            Compiled output"
	@echo "    build/               Intermediate artifacts"
	@echo ""
	@echo "  Targets:"
	@echo "    make                 Build all presentations"
	@echo "    make ENGINE=lualatex Build with lualatex"
	@echo "    make ENGINE=xelatex  Build with xelatex"
	@echo "    make list            List discovered presentations"
	@echo "    make clean           Remove build artifacts"
	@echo "    make distclean       Remove artifacts + PDFs"
	@echo "    make help            Show this help"
	@echo ""
	@echo "  Detected $(words $(TEX_FILES)) presentation(s):"
	@$(foreach t,$(TEX_FILES),echo "    $(t)";)
	@echo ""
