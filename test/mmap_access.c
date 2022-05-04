#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>


static void usage(const char *prog_name)
{
	fprintf(stderr, "%s dev offset size [value]\n", prog_name);
	fprintf(stderr, "   dev     device to use for instance /dev/akida0\n"
	                "   offset  offset in devive BAR0 area\n"
	                "   size    Access size (8, 16, 32)\n"
	                "   value   if present write value at given offset\n"
	                "           if not present, read the given offset\n");
}

int strtou32(const char *str, uint32_t *val)
{
	unsigned long long tmp;
	char *tail;

	errno = 0;
	tmp = strtoull(str, &tail, 0);
	if (errno)
		return errno;

	if ((tail == str) || (*tail != '\0'))
		return EINVAL;

	if (tmp > UINT32_MAX)
		return ERANGE;

	*val = tmp;
	return 0;
}

int main(int argc, char* argv[])
{
	const char *devpath;
	uint32_t offset;
	int access_size;
	uint32_t value;
	int is_write;
	void *addr;
	int err;
	int fd;

	if ((argc < 4) || (argc > 5)) {
		usage(argv[0]);
		return 1;
	}

	devpath = argv[1];
	err = strtou32(argv[2], &offset);
	if (err) {
		usage(argv[0]);
		return 1;
	}
	if (!strcmp(argv[3],"8")) {
		access_size = 8;
	} else if (!strcmp(argv[3],"16")) {
		access_size = 16;
	} else if (!strcmp(argv[3],"32")) {
		access_size = 32;
	} else {
		usage(argv[0]);
		return 1;
	}
	is_write = 0;
	if (argc > 4) {
		err = strtou32(argv[4], &value);
		if (err) {
			usage(argv[0]);
			return 1;
		}
		is_write = 1;
	}

	fd = open(devpath, O_RDWR);
	if (fd < 0) {
		err = errno;
		fprintf(stderr,"open(%s) failed (%d-%s)\n",
			devpath, err, strerror(err));
		return 1;
	}

	addr = mmap(NULL, 4*1024*1024, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	close(fd);
	if (addr == MAP_FAILED) {
		err = errno;
		fprintf(stderr,"mmap(%s) failed (%d-%s)\n",
			devpath, err, strerror(err));
		return 1;
	}

	if (is_write) {
		switch (access_size) {
		case 8:  *((volatile uint8_t *) (addr + offset)) = value; break;
		case 16: *((volatile uint16_t *)(addr + offset)) = value; break;
		case 32: *((volatile uint32_t *)(addr + offset)) = value; break;
		}
	} else {
		switch (access_size) {
		case 8:  value = *((volatile uint8_t *) (addr + offset)); break;
		case 16: value = *((volatile uint16_t *)(addr + offset)); break;
		case 32: value = *((volatile uint32_t *)(addr + offset)); break;
		}
		printf("0x%x\n", value);
	}

	munmap(addr, 4*1024*1024);

	return 0;
}
