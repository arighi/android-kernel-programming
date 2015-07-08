#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
	unsigned long long page_size = getpagesize();
	unsigned long long page_mask = ~(page_size - 1);
	unsigned long long addr, offset;
	unsigned int *ptr;
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: %s ADDR\n", argv[0]);
		return -1;
	}
	addr = strtoll(argv[1], NULL, 0);
	if (!addr) {
		fprintf(stderr, "Invalid address\n");
		return -1;
	}

	fd = open("/dev/rawmem", O_RDONLY | O_SYNC | O_LARGEFILE);
	if (fd < 0) {
		fprintf(stderr, "Error opening rawmem\n");
		return -1;
	}
	ptr = (unsigned int *)mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd,
					  addr & page_mask);
	if (ptr == NULL) {
		fprintf(stderr, "failed to mmap\n");
		return -1;
	}
	offset = (addr & ~page_mask) / sizeof(*ptr);
	fprintf(stdout, "[%p -> %#llx] = %#x\n", ptr + offset, addr, ptr[offset]);
	fflush(stdout);

	munmap(ptr, page_size);
	close(fd);

	return 0;
}
