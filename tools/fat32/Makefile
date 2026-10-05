# tools/fat32/Makefile - build the FAT32 parser host test and run it.
#
# fat_host_test compiles kernel/fs/tiku_fat.c itself, which builds on the host
# because it has no hardware includes.  `make check` runs it on the images in
# CORPUS, and builds them with mkcorpus.sh when that directory does not exist.
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
