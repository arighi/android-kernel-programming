/*
 * fchar-test: fast character device testcase
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Copyright (C) 2015 Andrea Righi <righi.andrea@gmail.com>
 */

#define _GNU_SOURCE
#define __USE_GNU

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fadvise.h>

#include "fchar.h"

#define MIN(a, b) (a < b) ? a : b

static char *filename;
static int use_mmap;
static int iterations;

static inline unsigned long long timeval(const struct timeval * time)
{
	return time->tv_sec * 1000000UL + time->tv_usec;
}

static inline float evaluate_bw(float size, float usec)
{
	return (size * 1E6) / usec;
}

static void do_rw(ssize_t size)
{
	void *buf;
	ssize_t sz, page_size;
	int i;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0) {
		perror("sysconf");
		exit(EXIT_FAILURE);
	}
        buf = alloca(page_size);
	memset(buf, 0xa0, page_size);

	for (i = 0; i < iterations; i++) {
		struct timeval start, stop, diff;
		unsigned long long time;

		sz = size;
		lseek(fileno(stdout), 0, SEEK_SET);

		gettimeofday(&start, NULL);
		while (sz > 0) {
			ssize_t written;

			written = write(fileno(stdout), buf,
					MIN(size, page_size));
			fprintf(stderr, "written=%d, sz=%d\n", written, sz);
			if (written < 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
			sz -= written;
		}
		gettimeofday(&stop, NULL);
		timersub(&stop, &start, &diff);

		time = timeval(&diff);

		fprintf(stderr,
			"memset(): %-2d write %Lu usec [%.02f MiB/s]\n",
			i, time, evaluate_bw((float)size, (float)time) / 1E6);
	}
}

static void do_mmap(ssize_t size)
{
        void *buf;
	int i;

	buf = mmap(NULL, size,
			PROT_READ | PROT_WRITE, MAP_SHARED, fileno(stdout), 0);
        if ((ssize_t)buf < 0) {
		perror("mmap");
		exit(EXIT_FAILURE);
        }

	for (i = 0; i < iterations; i++) {
		struct timeval start, stop, diff;
		unsigned long long time;

		gettimeofday(&start, NULL);
		memset(buf, 0xa0, size);
		gettimeofday(&stop, NULL);
		timersub(&stop, &start, &diff);

		time = timeval(&diff);

		fprintf(stderr,
			"memset(): %-2d mmap() write %Lu usec [%.02f MiB/s]\n",
			i, time, evaluate_bw((float)size, (float)time) / 1E6);
	}

	if (munmap(buf, size) < 0) {
		perror("munmap");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	ssize_t size;
	int fd;

	if (argc < 5) {
		fprintf(stderr,
			"%s DEVICE SIZE ITERATIONS USE_MMAP\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	filename = argv[1];
	size = strtol(argv[2], (char **)NULL, 10);
	iterations = strtol(argv[3], (char **)NULL, 10);
	use_mmap = strtol(argv[4], (char **)NULL, 10);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

        if ((fd = open(filename, O_RDWR | O_SYNC)) < 0) {
		perror("open");
		exit(EXIT_FAILURE);
        }
	if (!size) {
		if (ioctl(fd, FCHAR_IOCGSIZE, &size) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
	}
	dup2(fd, fileno(stdout));

	if (use_mmap)
		do_mmap(size);
	else
		do_rw(size);
	close(fd);

	return 0;
}
