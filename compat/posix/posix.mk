# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
#
# compat/posix: the POSIX library's sources, by group.  A port's makefile
# includes this file when TIKU_POSIX_ENABLE=1 and adds POSIX_SRCS to its
# sources; with the option unset nothing here reaches the build.
POSIX_SRCS := compat/posix/tiku_posix_time.c
