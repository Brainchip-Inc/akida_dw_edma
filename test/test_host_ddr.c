#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "spdk/barrier.h"

#define barrier_rmb	spdk_rmb
#define barrier_wmb	spdk_wmb
#define barrier_mb	spdk_mb

struct mmap_area {
	void *virt_addr;
	uint32_t phy_addr;
	size_t size;
};

static int mmap_area_init(struct mmap_area *mmap_area, const char *devpath, uint32_t phy_addr, size_t size)
{
	int fd;
	int err;

	memset(mmap_area, 0, sizeof(*mmap_area));

	fd = open(devpath, O_RDWR);
	if (fd < 0) {
		err = errno;
		fprintf(stderr,"open(%s) failed (%d-%s)\n",
			devpath, err, strerror(err));
		return err;
	}

	/* mmap the area */
	mmap_area->virt_addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, phy_addr);

	close(fd);
	if (mmap_area->virt_addr == MAP_FAILED) {
		err = errno;
		fprintf(stderr,"mmap(%s, %zu, 0x%"PRIx32") failed (%d-%s)\n",
			devpath, size, phy_addr, err, strerror(err));
		return err;
	}

	mmap_area->phy_addr = phy_addr;
	mmap_area->size = size;

	return 0;
}

static void mmap_area_exit(struct mmap_area *mmap_area)
{
	/* unmap the area */
	munmap(mmap_area->virt_addr, mmap_area->size);
}

static uint32_t mmap_area_virt2phy(struct mmap_area *mmap_area, void *virt_addr)
{
	ptrdiff_t offset;

	offset = virt_addr - mmap_area->virt_addr;

	return mmap_area->phy_addr + offset;
}

static void io_write32(void *addr, uint32_t val)
{
	barrier_wmb();
	*((volatile uint32_t *)addr) = val;
}

static uint32_t io_read32(void *addr)
{
	uint32_t val;

	val = *((volatile uint32_t *)addr);
	barrier_rmb();

	return val;
}

static void timestamp_get(struct timespec *t)
{
	clock_gettime(CLOCK_MONOTONIC_RAW, t);
}

static void timestamp_from_usec(struct timespec *t, useconds_t usec)
{
	if (usec < 1000000) {
		t->tv_sec = 0;
		t->tv_nsec = usec;
		t->tv_nsec *= 1000;
		return;
	}

	t->tv_sec = usec / 1000000;
	t->tv_nsec = usec % 1000000;
	t->tv_nsec *= 1000;
}

static void timestamp_delta(struct timespec *tdelta, const struct timespec *tstart,
			    const struct timespec *tend)
{
	tdelta->tv_sec = tend->tv_sec - tstart->tv_sec;
	tdelta->tv_nsec = tend->tv_nsec - tstart->tv_nsec;
	if (tdelta->tv_nsec < 0) {
		tdelta->tv_sec--;
		tdelta->tv_nsec += 1000000000;
	}
}

static int timestamp_cmp(const struct timespec *t1, const struct timespec *t2)
{
	/*
	 * Returns:
	 *  - negative if t1 < t2
	 *  - 0 if t1 == t2
	 *  - positive if t1 > t2
	 */
	if (t1->tv_sec == t2->tv_sec) {
		if (t1->tv_nsec > t2->tv_nsec)
			return 1;
		if (t1->tv_nsec < t2->tv_nsec)
			return -1;
		return 0;
	}
	if (t1->tv_sec < t2->tv_sec)
		return -1;
	return 1;
}

static int timestamp_is_timeout(const struct timespec *tstart, const struct timespec *timeout)
{
	struct timespec delta;

	timestamp_get(&delta);
	timestamp_delta(&delta, tstart, &delta);
	return timestamp_cmp(&delta, timeout) > 0;
}

static void timestamp_print(const char *txt_prefix, const struct timespec *t)
{
	printf("%s%ld.%06lds\n", txt_prefix, t->tv_sec, t->tv_nsec/1000);
}

static void timestamp_print_delta(const char *txt_prefix, const struct timespec *tstart,
				  const struct timespec *tend)
{
	struct timespec tdelta;

	timestamp_delta(&tdelta, tstart, tend);
	timestamp_print(txt_prefix, &tdelta);
}


struct dma_descriptor {
	uint32_t ctrl;
	uint32_t src;
	uint32_t size;
	uint32_t dst;
} __attribute__((packed));

static void dma_reset(struct mmap_area *dma)
{
	void *dma_base = dma->virt_addr;

	/* AKD1500 DMA Reset */
	io_write32(dma_base, 0x83000200);
}

static void dma_init(struct mmap_area *dma, uint32_t first_desc_addr)
{
	void *dma_base = dma->virt_addr;

	/* Set up the DMA controller: Enable DMA and loopback mode */
	io_write32(dma_base, 0x83000502);
	io_write32(dma_base + 0x0b0, 0x00000001);

	/* Set the starting DMA descriptor */
	io_write32(dma_base + 0x008, first_desc_addr);
}

static void dma_start(struct mmap_area *dma, uint32_t desc_number)
{
	void *dma_base = dma->virt_addr;

	/*
	 * Be sure that data and descriptor accesses done
	 * barrier_wmb();
	 * This will be done by io_write32() here after.
	 * -> Not needed here
	 */

	/* Start the DMA with descriptor #0 */
	io_write32(dma_base + 0x004, 0x00000000);
}

static int dma_wait(struct mmap_area *dma, useconds_t usec_timeout)
{
	void *dma_base = dma->virt_addr;
	struct timespec tstart, timeout;

	timestamp_from_usec(&timeout, usec_timeout);
	timestamp_get(&tstart);

	/* Wait for the end of DMA */
	while ((io_read32(dma_base + 0x028) & 0x00000003) != 0x00000003) {
		if (timestamp_is_timeout(&tstart, &timeout)) {
			fprintf(stderr,"DMA transfer timeout\n");
			return ETIMEDOUT;
		}
	}

	/*
	 * Be sure that all memory read will be done after
	 * barrier_rmb();
	 * This is already done by io_read32()
	 * -> Not needed here
	 */

	return 0;
}

static const uint32_t pattern[] = {
	0xCAFEDECA,
	0xDEADBEAF,
	0x12345678,
	0x55AA00FF,
};

enum test_result {
	TEST_OK,
	TEST_FAILED,
	TEST_NOTDONE,
};

static enum test_result test_host_ddr_simple(struct mmap_area *ddr, struct mmap_area *dma, int is_verbose, unsigned long param)
{
	struct timespec tstart, tend;
	struct dma_descriptor *desc;
	uint32_t *data_src;
	uint32_t *data_dst;
	uint32_t tmp;
	int count;
	int err;

	if (ddr->size < 0x3000 + ((8 + 4) * sizeof(uint32_t))) {
		printf("   min ddr size needed: %zu bytes\n",
			0x3000 + ((8 + 4) * sizeof(uint32_t)));
		return TEST_NOTDONE;
	}

	desc     = ddr->virt_addr + 0x1000;
	data_src = ddr->virt_addr + 0x2000;
	data_dst = ddr->virt_addr + 0x3000;

	/* AKD1500 DMA Reset, issued from RC */
	dma_reset(dma);

	if (is_verbose)
		printf("   xfer size: %zu (0x%zx) bytes\n",
			4 * sizeof(uint32_t), 4 * sizeof(uint32_t));

	timestamp_get(&tstart);

	/* Initialize the source data */
	memcpy(data_src, pattern, 4 * sizeof(uint32_t));

	timestamp_get(&tend);
	if (is_verbose)
		timestamp_print_delta("   prepare src data duration: ", &tstart, &tend);

	/* Zeroize the destination data */
	memset(data_dst,0, (8 + 4) * sizeof(uint32_t));

	/* Set the DMA descriptor */
	desc->ctrl = 0;
	desc->src  = mmap_area_virt2phy(ddr, data_src);
	desc->size = 4;
	desc->dst  = mmap_area_virt2phy(ddr, data_dst);

	/* Initialize the DMA controller */
	dma_init(dma, mmap_area_virt2phy(ddr, desc));

	timestamp_get(&tstart);

	/* Start the DMA with descriptor #0 */
	dma_start(dma, 0);

	/* Wait for the end of DMA */
	err = dma_wait(dma, 1*1000*1000);
	if (err) {
		fprintf(stderr,"dma_wait() failed (%d-%s)\n",
			err, strerror(err));
		return TEST_FAILED;
	}

	timestamp_get(&tend);
	if (is_verbose)
		timestamp_print_delta("   DMA duration: ", &tstart, &tend);

	timestamp_get(&tstart);

	/* Checked destination data */
	for (count = 0; count < 4; count++) {
		tmp = *(data_dst + 8 + count);
		if (tmp != pattern[count]) {
			fprintf(stderr,"dest[%d] = 0x%"PRIx32" != 0x%"PRIx32"\n",
				count, tmp, pattern[count]);
			return TEST_FAILED;
		}
	}

	timestamp_get(&tend);
	if (is_verbose)
		timestamp_print_delta("   check dst data duration: ", &tstart, &tend);

	return TEST_OK;
}

static enum test_result test_host_ddr_size(struct mmap_area *ddr, struct mmap_area *dma, int is_verbose, unsigned long param)
{
	struct timespec tstart, tend;
	struct dma_descriptor *desc;
	uint32_t *data_src;
	uint32_t *data_dst;
	uint32_t read, exp;
	size_t data_size;
	size_t count;
	int err;

	/* Host DDR : max 16 MB (0x01000000)
	 * @0x00000000-0x0000001f: one DMA descriptor
	 * @0x00000020-0x007fffff: data src size up to 0x7fffe0
	 * @0x00800000-0x00ffffff: data dst size 0x20 + up to 0x7fffe0
	 */
	data_size = param;
	if (ddr->size < (data_size + 0x20) * 2) {
		printf("   min ddr size needed: %zu bytes\n", (data_size + 0x20) * 2);
		return TEST_NOTDONE;
	}
	if (data_size % 4) {
		printf("   data size 0x%zx must be aligned on 4 bytes\n", data_size);
		return TEST_NOTDONE;
	}
	desc = ddr->virt_addr;
	data_src = ddr->virt_addr + 0x00000020;
	data_dst = ddr->virt_addr + 0x20 + data_size;

	/* AKD1500 DMA Reset, issued from RC */
	dma_reset(dma);

	if (is_verbose)
		printf("   xfer size: %zu (0x%zx) bytes\n", data_size, data_size);

	timestamp_get(&tstart);

	/* Initialize the source data */
	for (count = 0; count < data_size/sizeof(uint32_t); count++)
		*(data_src + count) = count;

	timestamp_get(&tend);
	if (is_verbose)
		timestamp_print_delta("   prepare src data duration: ", &tstart, &tend);

	/* Zeroize the destination data */
	memset(data_dst,0, 8 * sizeof(uint32_t) + data_size);

	/* Set the DMA descriptor */
	desc->ctrl = 0;
	desc->src  = mmap_area_virt2phy(ddr, data_src);
	desc->size = data_size/sizeof(uint32_t);
	desc->dst  = mmap_area_virt2phy(ddr, data_dst);

	/* Initialize the DMA controller */
	dma_init(dma, mmap_area_virt2phy(ddr, desc));

	timestamp_get(&tstart);

	/* Start the DMA with descriptor #0 */
	dma_start(dma, 0);

	/* Wait for the end of DMA */
	err = dma_wait(dma, 1*1000*1000);
	if (err) {
		fprintf(stderr,"dma_wait() failed (%d-%s)\n",
			err, strerror(err));
		return TEST_FAILED;
	}

	timestamp_get(&tend);
	if (is_verbose)
		timestamp_print_delta("   DMA duration: ", &tstart, &tend);

	timestamp_get(&tstart);

	/* Checked destination data */
	for (count = 0; count < data_size / sizeof(uint32_t); count++) {
		read = *(data_dst + 8 + count);
		exp = *(data_src + count);
		if (read != exp) {
			fprintf(stderr,"dest[%zu] = 0x%"PRIx32" != 0x%"PRIx32"\n",
				count, read, exp);
			return TEST_FAILED;
		}
	}

	timestamp_get(&tend);
	if (is_verbose)
		timestamp_print_delta("   check dst data duration: ", &tstart, &tend);

	return TEST_OK;
}

static const char *test_result2str(enum test_result result)
{
	switch (result) {
	case TEST_OK: return "ok";
	case TEST_FAILED: return "FAILED";
	case TEST_NOTDONE: return "Not Done";
	default: break;
	}
	return "???";
}

struct tests_stats {
	int total;
	int ok;
	int failed;
	int not_done;
};

static void update_tests_stats(struct tests_stats *stats, enum test_result result)
{
	stats->total++;

	switch (result) {
	case TEST_OK:
		stats->ok++;
		break;
	case TEST_FAILED:
		stats->failed++;
		break;
	case TEST_NOTDONE:
		stats->not_done++;
		break;
	}
}

static int do_tests(struct mmap_area *ddr, struct mmap_area *dma, struct tests_stats *stats, int is_verbose)
{
	const struct test_def {
		const char *name;
		enum test_result (*tst_fct)(struct mmap_area *ddr, struct mmap_area *dma,
					    int is_verbose, unsigned long param);
		unsigned long param;
	} tab_test[] = {
		{ "test_host_ddr simple", test_host_ddr_simple, 0 },
		{ "test_host_ddr   32", test_host_ddr_size, 32 },
		{ "test_host_ddr  256", test_host_ddr_size, 256 },
		{ "test_host_ddr 1236", test_host_ddr_size, 1236 },
		{ "test_host_ddr 4096", test_host_ddr_size, 4096 },
		{ "test_host_ddr 8000", test_host_ddr_size, 8000 },
		{ "test_host_ddr  1MB", test_host_ddr_size, 1*1024*1024 },
		{ "test_host_ddr  2MB", test_host_ddr_size, 2*1024*1024 },
		{ "test_host_ddr  4MB", test_host_ddr_size, 4*1024*1024 },
		{ "test_host_ddr  max", test_host_ddr_size, 0x7fffe0 },
		{ 0}
	}, *test;
	enum test_result result;
	int failed = 0;

	test = tab_test;
	do {
		printf("-- %s\n", test->name);
		result = test->tst_fct(ddr, dma, is_verbose, test->param);
		printf("-- %s %s\n", test->name, test_result2str(result));

		update_tests_stats(stats, result);
		if (result == TEST_FAILED)
			failed++;
	} while ((++test)->name);

	return failed;
}

static void usage(const char *prog_name)
{
	fprintf(stderr, "%s dev\n", prog_name);
	fprintf(stderr, "   dev     Device to use for instance /dev/akd1500_0\n");
}

int main(int argc, char* argv[])
{
	struct tests_stats stats;
	const char *devpath;
	struct mmap_area dma;
	struct mmap_area ddr;
	int failed;
	int err;

	if ((argc < 2) || (argc > 2)) {
		usage(argv[0]);
		return 1;
	}

	devpath = argv[1];

	/* mmap the area related to the Akida DMA controller */
	err = mmap_area_init(&dma, devpath, 0xfcc20000, 4096);
	if (err) {
		fprintf(stderr,"mmap_area_init(%s, 0xfcc20000, 4096) failed (%d-%s)\n",
			devpath, err, strerror(err));
		return 1;
	}

	/* mmap the area related to the host DDR area */
	err = mmap_area_init(&ddr, devpath, 0xc0000000, 16*1024*1024);
	if (err) {
		fprintf(stderr,"mmap_area_init(%s, 0xc0000000, 8*1024*1024) failed (%d-%s)\n",
			devpath, err, strerror(err));
		mmap_area_exit(&dma);
		return 1;
	}

	/* Raz test stats */
	memset(&stats, 0, sizeof(stats));

	/* Do the tests */
	failed = do_tests(&ddr, &dma, &stats, 1);

	printf("results:\n");
	printf("- run    %d/%d\n", stats.total - stats.not_done, stats.total);
	printf("- ok     %d/%d\n", stats.ok, stats.total - stats.not_done);
	printf("- failed %d/%d\n", stats.failed, stats.total - stats.not_done);

	if (stats.ok + stats.failed + stats.not_done != stats.total) {
		printf("!!! ok + failed + not done != total (%d + %d + %d != %d)\n",
			stats.ok, stats.failed, stats.not_done, stats.total);
		failed = 1;
	}

	mmap_area_exit(&ddr);
	mmap_area_exit(&dma);
	return failed ? 1 : 0;
}
