# tools/usbmsc/Makefile - build and run the USB device-class host tests.
#
# The tests compile services/usb/tiku_usbd_msc.c and tiku_usbd_ctrl.c themselves,
# which build on the host because they have no controller includes.  make lint
# runs `make check` here.
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -I../..

all: msc_host_test msc_diff_test ctrl_host_test

msc_host_test: msc_host_test.c ../../services/usb/tiku_usbd_msc.c \
               ../../services/usb/tiku_usbd_msc.h
	$(CC) $(CFLAGS) -o $@ msc_host_test.c ../../services/usb/tiku_usbd_msc.c

# msc_diff_test runs tiku_usbd_msc.c beside ref_original.c, a frozen copy of
# the Apollo510 driver's SCSI logic, and fails on any difference.
msc_diff_test: msc_diff_test.c ref_original.c ref_original.h \
               ../../services/usb/tiku_usbd_msc.c \
               ../../services/usb/tiku_usbd_msc.h
	$(CC) $(CFLAGS) -o $@ msc_diff_test.c ref_original.c \
	      ../../services/usb/tiku_usbd_msc.c

ctrl_host_test: ctrl_host_test.c ../../services/usb/tiku_usbd_ctrl.c \
                ../../services/usb/tiku_usbd_ctrl.h
	$(CC) $(CFLAGS) -o $@ ctrl_host_test.c ../../services/usb/tiku_usbd_ctrl.c

check: msc_host_test msc_diff_test ctrl_host_test
	./msc_host_test
	@echo
	./msc_diff_test
	@echo
	./ctrl_host_test

clean:
	rm -f msc_host_test msc_diff_test ctrl_host_test
.PHONY: all check clean
