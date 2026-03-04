# ===========================================================================
#  TikuOS Presentations — Universal Makefile
# ===========================================================================
#
#  Builds all presentations: local .tex files in this directory AND every
#  subdirectory that contains its own Makefile.
#
#  Usage:
#    make                Build all presentations (default: pdflatex)
#    make pdflatex       Build all with pdflatex
#    make lualatex       Build all with lualatex
#    make xelatex        Build all with xelatex
#    make <subdir>       Build only one subdirectory (e.g. make memory)
#    make clean          Clean all build artifacts
#    make distclean      Clean all artifacts + output PDFs
#    make list           List discovered presentations
#    make help           Show this help
#
# ===========================================================================

# --- Configuration ---------------------------------------------------------
ENGINE     ?= pdflatex
VIEWER     ?= xdg-open
BUILD_DIR  := build

# --- Auto-discover presentations ------------------------------------------
# Local .tex files in this directory
LOCAL_TEX  := $(wildcard *.tex)
LOCAL_PDF  := $(LOCAL_TEX:.tex=.pdf)

# Subdirectories that contain a Makefile (exclude build/)
SUBDIRS    := $(sort $(patsubst %/Makefile,%,$(wildcard */Makefile)))
SUBDIRS    := $(filter-out build,$(SUBDIRS))

# Engine-specific flags (for local builds)
PDFLATEX_FLAGS := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)
LUALATEX_FLAGS := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)
XELATEX_FLAGS  := -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)

# --- Phony targets ---------------------------------------------------------
.PHONY: all pdflatex lualatex xelatex clean distclean list help
.PHONY: $(SUBDIRS) $(addsuffix -clean,$(SUBDIRS)) $(addsuffix -distclean,$(SUBDIRS))

# --- Default: build everything ---------------------------------------------
all: local-all subdirs-all

pdflatex: ENGINE := pdflatex
pdflatex: all

lualatex: ENGINE := lualatex
lualatex: all

xelatex: ENGINE := xelatex
xelatex: all

# --- Local .tex files ------------------------------------------------------
.PHONY: local-all
local-all: $(LOCAL_PDF)

%.pdf: %.tex
	@mkdir -p $(BUILD_DIR)
	@echo "==> [local] Building $< with $(ENGINE) (pass 1/2)..."
	@$(ENGINE) $($(shell echo $(ENGINE) | tr a-z A-Z)_FLAGS) $<
	@echo "==> [local] Building $< with $(ENGINE) (pass 2/2)..."
	@$(ENGINE) $($(shell echo $(ENGINE) | tr a-z A-Z)_FLAGS) $<
	@cp $(BUILD_DIR)/$@ .
	@echo "==> [local] Output: $@"

# --- Subdirectory builds ---------------------------------------------------
.PHONY: subdirs-all
subdirs-all: $(SUBDIRS)

$(SUBDIRS):
	@echo "==> [subdir] Building $@/ ..."
	@$(MAKE) -C $@ ENGINE=$(ENGINE) --no-print-directory

# --- Per-subdirectory targets (e.g. make memory-clean) ---------------------
$(addsuffix -clean,$(SUBDIRS)):
	@$(MAKE) -C $(patsubst %-clean,%,$@) clean --no-print-directory

$(addsuffix -distclean,$(SUBDIRS)):
	@$(MAKE) -C $(patsubst %-distclean,%,$@) distclean --no-print-directory

# --- Clean ------------------------------------------------------------------
clean: $(addsuffix -clean,$(SUBDIRS))
	@echo "==> [local] Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(foreach t,$(LOCAL_TEX),$(basename $(t)).{aux,log,nav,out,snm,toc,vrb})

distclean: $(addsuffix -distclean,$(SUBDIRS))
	@echo "==> [local] Cleaning all artifacts + PDFs..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(foreach t,$(LOCAL_TEX),$(basename $(t)).{aux,log,nav,out,snm,toc,vrb})
	@rm -f $(LOCAL_PDF)

# --- List -------------------------------------------------------------------
list:
	@echo ""
	@echo "  Discovered presentations"
	@echo "  ========================"
	@echo ""
	@echo "  Local .tex files:"
	@$(if $(LOCAL_TEX),$(foreach t,$(LOCAL_TEX),echo "    - $(t)";),echo "    (none)")
	@echo ""
	@echo "  Subdirectories with Makefile:"
	@$(if $(SUBDIRS),$(foreach d,$(SUBDIRS),echo "    - $(d)/";),echo "    (none)")
	@echo ""

# --- Help -------------------------------------------------------------------
help:
	@echo ""
	@echo "  TikuOS Presentations — Universal Build System"
	@echo "  =============================================="
	@echo ""
	@echo "  make                Build all presentations with pdflatex"
	@echo "  make pdflatex       Build all with pdflatex"
	@echo "  make lualatex       Build all with lualatex"
	@echo "  make xelatex        Build all with xelatex"
	@echo "  make <subdir>       Build a single subdirectory (e.g. make memory)"
	@echo "  make <subdir>-clean Clean a single subdirectory"
	@echo "  make clean          Clean all build artifacts"
	@echo "  make distclean      Clean all artifacts + output PDFs"
	@echo "  make list           List discovered presentations"
	@echo "  make help           Show this help"
	@echo ""
	@echo "  Variables:"
	@echo "    ENGINE=pdflatex   LaTeX engine (pdflatex|lualatex|xelatex)"
	@echo "    VIEWER=xdg-open   PDF viewer command"
	@echo ""
	@echo "  Detected:"
	@echo "    Local:  $(or $(LOCAL_TEX),(none))"
	@echo "    Subdirs: $(or $(SUBDIRS),(none))"
	@echo ""
