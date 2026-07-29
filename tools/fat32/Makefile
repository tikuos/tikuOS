# F0: build and run the FAT32 parser on the host.
#
# The parser under test is the REAL kernel/fs/tiku_fat.c -- not a copy, not a
# reimplementation.  That is the whole value of keeping it free of hardware
# includes.
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -I../..
CORPUS ?= /tmp/fat32corpus

all: fat_host_test

fat_host_test: fat_host_test.c ../../kernel/fs/tiku_fat.c ../../kernel/fs/tiku_fat.h
	$(CC) $(CFLAGS) -o $@ fat_host_test.c ../../kernel/fs/tiku_fat.c

corpus:
	./mkcorpus.sh $(CORPUS)

check: fat_host_test
	@test -d $(CORPUS) || $(MAKE) corpus
	./fat_host_test $(CORPUS)

clean:
	rm -f fat_host_test
.PHONY: all corpus check clean
