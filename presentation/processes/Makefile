# ===========================================================================
#  TikuOS Processes Presentation — LaTeX Build Makefile
# ===========================================================================
#
#  Usage:
#    make              Build PDF (default: pdflatex)
#    make pdflatex     Build with pdflatex
#    make lualatex     Build with lualatex
#    make xelatex      Build with xelatex
#    make view         Build and open PDF viewer
#    make watch        Continuous rebuild on file changes (needs inotifywait)
#    make clean        Remove all build artifacts
#    make distclean    Remove artifacts + output PDF
#    make help         Show this help
#
# ===========================================================================

# --- Configuration ---------------------------------------------------------
TEX_FILE   := tikuos_processes
ENGINE     ?= pdflatex
VIEWER     ?= xdg-open
BUILD_DIR  := build

# Engine-specific flags
PDFLATEX_FLAGS := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)
LUALATEX_FLAGS := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)
XELATEX_FLAGS  := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)

# --- Targets ---------------------------------------------------------------
.PHONY: all pdflatex lualatex xelatex view watch clean distclean help

all: $(TEX_FILE).pdf

# Build with pdflatex (default)
pdflatex: ENGINE := pdflatex
pdflatex: $(TEX_FILE).pdf

# Build with lualatex
lualatex: ENGINE := lualatex
lualatex: $(TEX_FILE).pdf

# Build with xelatex
xelatex: ENGINE := xelatex
xelatex: $(TEX_FILE).pdf

# --- Main build rule -------------------------------------------------------
$(TEX_FILE).pdf: $(TEX_FILE).tex
	@mkdir -p $(BUILD_DIR)
	@echo "==> Building with $(ENGINE) (pass 1/2)..."
	$(ENGINE) $($(shell echo $(ENGINE) | tr a-z A-Z)_FLAGS) $(TEX_FILE).tex
	@echo "==> Building with $(ENGINE) (pass 2/2)..."
	$(ENGINE) $($(shell echo $(ENGINE) | tr a-z A-Z)_FLAGS) $(TEX_FILE).tex
	@cp $(BUILD_DIR)/$(TEX_FILE).pdf .
	@echo "==> Output: $(TEX_FILE).pdf"

# --- View -------------------------------------------------------------------
view: $(TEX_FILE).pdf
	@echo "==> Opening $(TEX_FILE).pdf..."
	$(VIEWER) $(TEX_FILE).pdf &

# --- Watch for changes (requires inotifywait) ------------------------------
watch:
	@command -v inotifywait >/dev/null 2>&1 || \
		{ echo "Error: install inotify-tools (sudo apt install inotify-tools)"; exit 1; }
	@echo "==> Watching $(TEX_FILE).tex for changes (Ctrl-C to stop)..."
	@while true; do \
		$(MAKE) --no-print-directory all; \
		echo "==> Waiting for changes..."; \
		inotifywait -q -e modify $(TEX_FILE).tex; \
	done

# --- Clean ------------------------------------------------------------------
clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f $(TEX_FILE).{aux,log,nav,out,snm,toc,vrb}

distclean: clean
	rm -f $(TEX_FILE).pdf

# --- Help -------------------------------------------------------------------
help:
	@echo ""
	@echo "  TikuOS Processes Presentation Build System"
	@echo "  ==========================================="
	@echo ""
	@echo "  make              Build PDF with pdflatex (default)"
	@echo "  make pdflatex     Build PDF with pdflatex"
	@echo "  make lualatex     Build PDF with lualatex"
	@echo "  make xelatex      Build PDF with xelatex"
	@echo "  make view         Build and open in PDF viewer"
	@echo "  make watch        Continuous rebuild on file changes"
	@echo "  make clean        Remove build artifacts"
	@echo "  make distclean    Remove artifacts + output PDF"
	@echo "  make help         Show this help"
	@echo ""
	@echo "  Variables:"
	@echo "    ENGINE=pdflatex  LaTeX engine (pdflatex|lualatex|xelatex)"
	@echo "    VIEWER=xdg-open  PDF viewer command"
	@echo ""
