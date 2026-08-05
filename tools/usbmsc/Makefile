# U0: build and run the mass-storage wire format on the host.
#
# The code under test is the REAL kernel/usb/tiku_usbd_msc.c -- not a copy,
# not a reimplementation.  That is the whole value of keeping it free of
# controller includes: the same object the Apollo510 driver links is the one
# checked here, so a regression cannot hide behind "the test has its own copy".
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -I../..

all: msc_host_test msc_diff_test

msc_host_test: msc_host_test.c ../../kernel/usb/tiku_usbd_msc.c \
               ../../kernel/usb/tiku_usbd_msc.h
	$(CC) $(CFLAGS) -o $@ msc_host_test.c ../../kernel/usb/tiku_usbd_msc.c

# The oracle: the pre-extraction code, kept verbatim, so "behaviour is
# unchanged" is a measured claim rather than an assertion.
msc_diff_test: msc_diff_test.c ref_original.c ref_original.h \
               ../../kernel/usb/tiku_usbd_msc.c \
               ../../kernel/usb/tiku_usbd_msc.h
	$(CC) $(CFLAGS) -o $@ msc_diff_test.c ref_original.c \
	      ../../kernel/usb/tiku_usbd_msc.c

check: msc_host_test msc_diff_test
	./msc_host_test
	@echo
	./msc_diff_test

clean:
	rm -f msc_host_test msc_diff_test
.PHONY: all check clean
