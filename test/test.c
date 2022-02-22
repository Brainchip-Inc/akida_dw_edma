// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Brainchip.
 * Akida PCIe driver tests
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>


static void display_buffer(const char *msg, uint8_t *buff, size_t size)
{
	size_t i;

	printf("%s", msg);
	for (i = 0; i < size; i++) {
		printf("%02x ", *(buff + i));
	}
	printf("\n");
}

static int test1(int fd)
{
	uint8_t buff[8];
	size_t size;
	off_t offset;
	ssize_t ssize;
	int err;

	size = 4;
	offset = 0xf0000010;
	ssize = pread(fd, buff, size, offset);
	if (ssize < 0) {
		err = errno;
		fprintf(stderr,"pread(%zu,0x%lx) failed (%d-%s)\n",
			size, offset, err, strerror(err));
		return err;
	}
	if ( (size_t)ssize != size) {
		fprintf(stderr,"pread(%zu,0x%lx) returns %zd)\n",
			size, offset, ssize);
		return ECANCELED;
	}

	printf("Rd @0x%04lx, %zu\n", offset, size);
	display_buffer("  ", buff, size);

	return 0;
}

static int test2(int fd)
{
#define TEST2_BUFFER_SIZE 32
	uint8_t buff[2][TEST2_BUFFER_SIZE];
	size_t size;
	off_t offset;
	ssize_t ssize;
	int err;

	for (size = 0; size < TEST2_BUFFER_SIZE; size++) {
		buff[0][size] = size;
	}

	size = TEST2_BUFFER_SIZE;
	offset = 0x20400000;
	ssize = pwrite(fd, buff[0], size, offset);
	if (ssize < 0) {
		err = errno;
		fprintf(stderr,"pwrite(%zu,0x%lx) failed (%d-%s)\n",
			size, offset, err, strerror(err));
		return err;
	}
	if ( (size_t)ssize != size) {
		fprintf(stderr,"pwrite(%zu,0x%lx) returns %zd)\n",
			size, offset, ssize);
		return ECANCELED;
	}

	printf("Wr @0x%04lx, %zu\n", offset, size);
	display_buffer("  ", buff[0], size);

	size = TEST2_BUFFER_SIZE;
	offset = 0x20400000;
	ssize = pread(fd, buff[1], size, offset);
	if (ssize < 0) {
		err = errno;
		fprintf(stderr,"pread(%zu,0x%lx) failed (%d-%s)\n",
			size, offset, err, strerror(err));
		return err;
	}
	if ( (size_t)ssize != size) {
		fprintf(stderr,"pread(%zu,0x%lx) returns %zd)\n",
			size, offset, ssize);
		return ECANCELED;
	}

	printf("Rd @0x%04lx, %zu\n", offset, size);
	display_buffer("  ", buff[1], size);

	for (size = 0; size < TEST2_BUFFER_SIZE; size++) {
		if (buff[0][size] != buff[1][size]) {
			printf("Mismatch at offset %zu\n",size);
			return EILSEQ;
		}
	}
	printf("Data ok\n");

	return 0;
}

static int test3(int fd)
{
#define TEST3_BUFFER_SIZE (1*1024*1024/sizeof(uint32_t))
	uint32_t buff[2][TEST3_BUFFER_SIZE];
	size_t size;
	off_t offset;
	ssize_t ssize;
	int err;

	for (size = 0; size < TEST3_BUFFER_SIZE; size++) {
		buff[0][size] = size;
	}

	size = TEST3_BUFFER_SIZE * sizeof(buff[0][0]);
	offset = 0x20400000;
	ssize = pwrite(fd, buff[0], size, offset);
	if (ssize < 0) {
		err = errno;
		fprintf(stderr,"pwrite(%zu,0x%lx) failed (%d-%s)\n",
			size, offset, err, strerror(err));
		return err;
	}
	if ( (size_t)ssize != size) {
		fprintf(stderr,"pwrite(%zu,0x%lx) returns %zd)\n",
			size, offset, ssize);
		return ECANCELED;
	}

	printf("Wr @0x%04lx, %zu\n", offset, size);

	size = TEST3_BUFFER_SIZE * sizeof(buff[1][0]);
	offset = 0x20400000;
	ssize = pread(fd, buff[1], size, offset);
	if (ssize < 0) {
		err = errno;
		fprintf(stderr,"pread(%zu,0x%lx) failed (%d-%s)\n",
			size, offset, err, strerror(err));
		return err;
	}
	if ( (size_t)ssize != size) {
		fprintf(stderr,"pread(%zu,0x%lx) returns %zd)\n",
			size, offset, ssize);
		return ECANCELED;
	}

	printf("Rd @0x%04lx, %zu\n", offset, size);

	for (size = 0; size < TEST3_BUFFER_SIZE; size++) {
		if (buff[0][size] != buff[1][size]) {
			printf("Mismatch at offset %zu (read 0x%04"PRIx16", exp 0x%04"PRIx16"\n",
				size,
				buff[1][size],
				buff[0][size]);
			return EILSEQ;
		}
	}
	printf("Data ok\n");

	return 0;
}


int main(int argc, char* argv[])
{
	const struct test_def {
		const char *name;
		int (*tst_fct)(int fd);
	} tab_test[] = {
		{"test1", test1},
		{"test2", test2},
		{"test3", test3},
		{0}
	}, *test;
	const char *devpath;
	int fd;
	int err;
	int ret;
	int failed;

	if (argc < 1)
		devpath = "/dev/akida0";
	else
		devpath = argv[1];



	fd = open(devpath, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		err = errno;
		fprintf(stderr,"open(%s) failed (%d-%s)\n",
			devpath, err, strerror(err));
		return 1;
	}

	failed = 0;

	test = tab_test;
	do {
		printf("-- %s\n", test->name);
		err = test->tst_fct(fd);
		printf("-- %s %s\n", test->name, err ? "FAILED" : "ok");
		if (err)
			failed++;
	} while ((++test)->name);

	ret = failed ? 1 : 0;

	close(fd);
	return ret;
}
