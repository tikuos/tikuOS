# Shared compute sources, relative to the TikuOS repository root.
TIKU_COMPUTE_BACKEND ?= reference
TIKU_COMPUTE_TRACE ?= 0
TIKU_COMPUTE_SRCS := hal/compute/tiku_compute.c hal/compute/tiku_compute_ref.c
TIKU_COMPUTE_FLAGS := -DTIKU_COMPUTE_TRACE=$(TIKU_COMPUTE_TRACE)
ifeq ($(TIKU_COMPUTE_BACKEND),p4)
ifneq ($(TIKU_PLATFORM),esp32p4)
$(error The p4 compute backend requires the ESP32-P4 firmware platform)
endif
ifneq ($(TIKU_COMPUTE_P4_REV),1)
$(error The p4 compute backend requires TIKU_COMPUTE_P4_REV=1 for v1.x silicon)
endif
TIKU_COMPUTE_SRCS += arch/esp32p4/compute/tiku_compute_p4.c
TIKU_COMPUTE_FLAGS += -DTIKU_COMPUTE_BACKEND=1 -DTIKU_COMPUTE_P4_REV=1
else ifeq ($(TIKU_COMPUTE_BACKEND),mve)
TIKU_COMPUTE_SRCS += hal/compute/tiku_compute_mve.c
TIKU_COMPUTE_FLAGS += -DTIKU_COMPUTE_BACKEND=2
else ifeq ($(TIKU_COMPUTE_BACKEND),reference)
TIKU_COMPUTE_FLAGS += -DTIKU_COMPUTE_BACKEND=0
else
$(error TIKU_COMPUTE_BACKEND must be reference, p4 or mve)
endif
