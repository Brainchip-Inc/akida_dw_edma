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
#include <pthread.h>


static void display_buffer(const char *msg, uint8_t *buff, size_t size)
{
	size_t i;

	printf("%s", msg);
	for (i = 0; i < size; i++) {
		printf("%02x ", *(buff + i));
	}
	printf("\n");
}

static int test1(int fd, int is_verbose, const char *devpath, off_t test_area)
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

	if (is_verbose) {
		printf("Rd @0x%04lx, %zu\n", offset, size);
		display_buffer("  ", buff, size);
	}

	return 0;
}

static int test2(int fd, int is_verbose, const char *devpath, off_t test_area)
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
	offset = test_area;
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

	if (is_verbose) {
		printf("Wr @0x%04lx, %zu\n", offset, size);
		display_buffer("  ", buff[0], size);
	}

	size = TEST2_BUFFER_SIZE;
	offset = test_area;
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

	if (is_verbose) {
		printf("Rd @0x%04lx, %zu\n", offset, size);
		display_buffer("  ", buff[1], size);
	}

	for (size = 0; size < TEST2_BUFFER_SIZE; size++) {
		if (buff[0][size] != buff[1][size]) {
			printf("Mismatch at offset %zu\n",size);
			return EILSEQ;
		}
	}
	if (is_verbose)
		printf("Data ok\n");

	return 0;
}

static int test3(int fd, int is_verbose, const char *devpath, off_t test_area)
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
	offset = test_area;
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

	if (is_verbose)
		printf("Wr @0x%04lx, %zu\n", offset, size);

	size = TEST3_BUFFER_SIZE * sizeof(buff[1][0]);
	offset = test_area;
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

	if (is_verbose)
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
	if (is_verbose)
		printf("Data ok\n");

	return 0;
}


struct thread_param {
	const char *name;
	const char *devpath;
	int fd;
	int is_verbose;
	off_t test_area;
	unsigned int nb_loop;
	int err;
};

void *thread_fct(void *arg)
{
	const struct test_def {
		const char *name;
		int (*tst_fct)(int fd, int is_verbose, const char *devname, off_t test_area);
	} tab_test[] = {
		{"test2 0", test2},
		{"test2 1", test2},
		{"test3", test3},
		{0}
	}, *test;
	struct thread_param *p = arg;
	unsigned int loop;
	int err;

	for (loop = 0; loop < p->nb_loop; loop++) {

		test = tab_test;
		do {
			if (p->is_verbose)
				printf("%s: loop %u, run %s ...\n", p->name, loop, test->name);
			err = test->tst_fct(p->fd, 0, p->devpath, p->test_area);
			if (err)  {
				fprintf(stderr, "%s: loop %u, run %s failed\n",
					p->name, loop, test->name);
				goto end;
			}

			if (p->is_verbose)
				printf("%s: loop %u, run %s ok\n", p->name, loop, test->name);
		} while ((++test)->name);
	}

	err = 0;

end:
	p->err = err;
	return &p->err;
}

static int test4(int fd, int is_verbose, const char *devpath, off_t test_area)
{
	struct thread_param p[2] = {0};
	pthread_t thread_id[2];
	int fd1;
	int err, err2;
	int i, j;

	fd1 = open(devpath, O_RDWR | O_NONBLOCK);
	if (fd1 < 0) {
		err = errno;
		fprintf(stderr,"open(%s) failed (%d-%s)\n",
			devpath, err, strerror(err));
		return err;
	}

	p[0].name = "thread0";
	p[0].devpath = devpath;
	p[0].fd = fd;
	p[0].is_verbose = is_verbose;
	p[0].test_area = test_area;
	p[0].nb_loop = 10;
	p[0].is_verbose = is_verbose;
	p[0].err = EINPROGRESS;

	p[1].name = "thread1";
	p[1].devpath = devpath;
	p[1].fd = fd1;
	p[1].is_verbose = is_verbose;
	p[1].test_area = test_area + 1*1024*1024; /* Do not overlap with other thread */
	p[1].nb_loop = 10;
	p[1].is_verbose = is_verbose;
	p[1].err = EINPROGRESS;

	for (i = 0; i < 2; i++) {
		err = pthread_create(&thread_id[i], NULL, thread_fct, &p[i]);
		if (err) {
			fprintf(stderr,"pthread_create(%d) failed (%d-%s)\n",
				i, err, strerror(err));
			for (j = 0; j < i; j++)
				pthread_join(thread_id[j], NULL);
			goto end;
		}
	}

	err = 0;
	for (i = 0; i < 2; i++) {
		err2 = pthread_join(thread_id[i], NULL);
		if (err2) {
			fprintf(stderr,"pthread_join(%d) failed (%d-%s)\n",
				i, err2, strerror(err2));
			if (!err)
				err = err2;
		}
	}
	if (err)
		goto end;

	err = 0;
	for (i = 0; i < 2; i++) {
		if (p[i].err) {
			err = p[i].err;
			goto end;
		}
	}

end:
	close(fd1);
	return err;
}


int main(int argc, char* argv[])
{
	const struct test_def {
		const char *name;
		int (*tst_fct)(int fd, int is_verbose, const char *devname, off_t test_area);
	} tab_test[] = {
		{"test1", test1},
		{"test2", test2},
		{"test3", test3},
		{"test4", test4},
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
		err = test->tst_fct(fd, 1, devpath, 0x20400000);
		printf("-- %s %s\n", test->name, err ? "FAILED" : "ok");
		if (err)
			failed++;
	} while ((++test)->name);

	ret = failed ? 1 : 0;

	close(fd);
	return ret;
}
