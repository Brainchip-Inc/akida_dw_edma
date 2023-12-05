// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Brainchip.
 * Akida PCIe driver
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma/edma.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-epf.h>
#include <linux/pci_ids.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 4, 0)
#include <linux/pci-aspm.h>
#endif

#include "dw-edma-core.h"
#include "akida-edma.h"

static DEFINE_IDA(akida_devno);

/* The DMA RAM area contains eDMA linked-list (LL) and data (DT).
 * This area is used by the eDMA controler and is located inside the device.
 * This physical address is from the eDMA point of view
 */
#ifndef CFG_AKIDA_DMA_RAM_PHY
#define AKIDA_DMA_RAM_PHY_ADDR	0x20000000
#define AKIDA_DMA_RAM_PHY_OFFSET       0x0

/* Linked-list: 64 bytes per channel */
#define AKIDA_DMA_RAM_PHY_TX0_LL_OFFSET    0x00 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_TX0_LL_SIZE      0x40
#define AKIDA_DMA_RAM_PHY_TX1_LL_OFFSET    0x40 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_TX1_LL_SIZE      0x40
#define AKIDA_DMA_RAM_PHY_RX0_LL_OFFSET    0x80 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_RX0_LL_SIZE      0x40
#define AKIDA_DMA_RAM_PHY_RX1_LL_OFFSET    0xC0 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_RX1_LL_SIZE      0x40

/* Data: Empty */
#define AKIDA_DMA_RAM_PHY_TX0_DT_OFFSET    0x0 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_TX0_DT_SIZE        0
#define AKIDA_DMA_RAM_PHY_TX1_DT_OFFSET    0x0 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_TX1_DT_SIZE        0
#define AKIDA_DMA_RAM_PHY_RX0_DT_OFFSET    0x0 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_RX0_DT_SIZE        0
#define AKIDA_DMA_RAM_PHY_RX1_DT_OFFSET    0x0 + AKIDA_DMA_RAM_PHY_OFFSET
#define AKIDA_DMA_RAM_PHY_RX1_DT_SIZE        0

#define AKIDA_DMA_RAM_PHY_SIZE 0x00000100 /* 256B */
#endif

#define AKIDA_DMA_RAM_PHY_LL_OFFSET(t,i) AKIDA_DMA_RAM_PHY_##t##i##_LL_OFFSET
#define AKIDA_DMA_RAM_PHY_LL_SIZE(t,i)   AKIDA_DMA_RAM_PHY_##t##i##_LL_SIZE
#define AKIDA_DMA_RAM_PHY_DT_OFFSET(t,i) AKIDA_DMA_RAM_PHY_##t##i##_DT_OFFSET
#define AKIDA_DMA_RAM_PHY_DT_SIZE(t,i)   AKIDA_DMA_RAM_PHY_##t##i##_DT_SIZE

/* Maximum DMA transfer chunk size */
#define AKIDA_DMA_XFER_MAX_SIZE  1024

#define AKIDA_1500_BAR2_OFFSET 0xFCC00000
#define AKIDA_1500_BAR4_OFFSET 0x20000000
#define AKIDA_1500_HOST_DDR_BASE 0xC0000000
#define AKIDA_1500_HOST_DDR_SIZE SZ_4M
#define AKIDA_1500_HOST_DDR_DMA_ATTRS DMA_ATTR_NO_KERNEL_MAPPING

struct akida_dma_chan {
	struct dma_chan *chan;
	struct completion dma_complete;
	enum dma_transfer_direction dma_xfer_dir;
	enum dma_data_direction dma_data_dir;
	dma_addr_t dma_buf;
	size_t dma_len;
	bool is_used;
};

struct akida_dev {
	struct pci_dev *pdev;
	int devno;
	struct miscdevice miscdev;
	struct dw_edma_chip edma_chip;
	struct akida_dma_chan rxchan[2];
	struct akida_dma_chan txchan[2];
	wait_queue_head_t wq_rxchan;
	wait_queue_head_t wq_txchan;
	void __iomem *mmio_bar0;
	struct {
		void *cpu_addr;
		dma_addr_t dma_addr;
	} host_ddr;
};

enum {
	AKIDA_1000 = 0,
	AKIDA_1500 = 1,
};

static void akida_dma_callback(void *arg)
{
	struct akida_dma_chan *dma_chan = arg;

	dma_unmap_single(dma_chan->chan->device->dev,
		dma_chan->dma_buf, dma_chan->dma_len, dma_chan->dma_data_dir);
	complete(&dma_chan->dma_complete);
}

static int akida_dma_transfer(struct akida_dev *akida,
	struct akida_dma_chan *dma_chan, phys_addr_t dev_addr,
	size_t len, void *buf)
{
	struct dma_slave_config dma_sconfig = {0};
	struct dma_async_tx_descriptor *txdesc;
	struct device *chan_dev;
	int ret;

	/* Set parameters
	 * DMA_MEM_TO_MEM is set as direction in order to be sure that the
	 * dw-edma engine will worked in remote initiator mode.
	 */
	dma_sconfig.direction = DMA_MEM_TO_MEM;
	switch (dma_chan->dma_xfer_dir) {
	case DMA_MEM_TO_DEV:
		dma_sconfig.dst_addr = dev_addr;
		dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	case DMA_DEV_TO_MEM:
		dma_sconfig.src_addr = dev_addr;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		pci_err(akida->pdev, "Unsupported direction (%d)\n",
			dma_chan->dma_xfer_dir);
		return -EINVAL;
	}

	/* Map buffer */
	dma_chan->dma_len = len;
	chan_dev = dma_chan->chan->device->dev;
	dma_chan->dma_buf = dma_map_single(chan_dev, buf, dma_chan->dma_len,
					   dma_chan->dma_data_dir);
	if (dma_mapping_error(chan_dev, dma_chan->dma_buf)) {
		pci_err(akida->pdev, "DMA mapping failed\n");
		return -EINVAL;
	}

	/* Update dma_sconfig
	 * Upstream commit 05655541c950 - dmaengine: dw-edma: Fix scatter-gather address calculation
	 * This commit was backported to 5.4.x
	 * Upstream commit b671d098a97f - dmaengine: dw-edma: Revert fix scatter-gather address calculation
	 * Revert the modification but was not backported.
	 *
	 * We set here some src_addr/dst_addr address to be compliant with
	 * kernels with or without the modification.
	 */
	if (dma_chan->dma_xfer_dir == DMA_MEM_TO_DEV)
		dma_sconfig.src_addr = dma_chan->dma_buf;
	else
		dma_sconfig.dst_addr = dma_chan->dma_buf;

	ret = dmaengine_slave_config(dma_chan->chan, &dma_sconfig);
	if (ret < 0) {
		pci_err(akida->pdev, "DMA slave config failed (%d)\n", ret);
		goto err;
	}

	/* Prepare transaction */
	txdesc = dmaengine_prep_slave_single(dma_chan->chan,
					     dma_chan->dma_buf, dma_chan->dma_len,
					     dma_chan->dma_xfer_dir,
					     DMA_PREP_INTERRUPT);
	if (!txdesc) {
		pci_err(akida->pdev, "Not able to get desc for DMA xfer\n");
		ret = -EINVAL;
		goto err;
	}

	/* Clear completion */
	reinit_completion(&dma_chan->dma_complete);

	/* Submit transaction */
	txdesc->callback = akida_dma_callback;
	txdesc->callback_param = dma_chan;
	ret = dma_submit_error(dmaengine_submit(txdesc));
	if (ret < 0) {
		pci_err(akida->pdev, "DMA submit failed\n");
		goto err;
	}

	/* Start transactions */
	dma_async_issue_pending(dma_chan->chan);

	/* Wait for completion */
	ret = wait_for_completion_timeout(&dma_chan->dma_complete,
					  msecs_to_jiffies(2000));
	if (!ret) {
		pci_err(akida->pdev, "DMA wait completion timed out\n");
		dmaengine_terminate_all(dma_chan->chan);
		ret = -ETIMEDOUT;
		goto err;
	}

	/* Ok, everything is done (unmap done in dma transaction callback) */
	return 0;

err:
	dma_unmap_single(chan_dev, dma_chan->dma_buf, dma_chan->dma_len,
			 dma_chan->dma_data_dir);
	return ret;
}

static bool akida_is_allowed(phys_addr_t addr, size_t size)
{
	/* Overlap with DMA RAM reserved area is not allowed */
	return (addr+size) < (AKIDA_DMA_RAM_PHY_ADDR + AKIDA_DMA_RAM_PHY_OFFSET)  ||
		(AKIDA_DMA_RAM_PHY_ADDR + AKIDA_DMA_RAM_PHY_OFFSET + AKIDA_DMA_RAM_PHY_SIZE) <= addr;
}

static struct akida_dma_chan *akida_get_unsused_chan(
					struct akida_dma_chan *tab_chan,
					unsigned int nb_chan)
{
	unsigned int i;

	for (i = 0; i < nb_chan; i++) {
		if (!(tab_chan+i)->is_used)
			return tab_chan+i;
	}
	return NULL;
}

static struct akida_dma_chan *akida_acquire_chan(wait_queue_head_t *wq,
					struct akida_dma_chan *tab_chan,
					unsigned int nb_chan)
{
	struct akida_dma_chan *chan;
	int ret;

	spin_lock(&wq->lock);

	ret = wait_event_interruptible_locked(*wq,
		(chan = akida_get_unsused_chan(tab_chan, nb_chan)));
	if (ret) {
		spin_unlock(&wq->lock);
		return ERR_PTR(ret);
	}

	chan->is_used = true;

	spin_unlock(&wq->lock);

	return chan;
}

static void akida_release_chan(wait_queue_head_t *wq, struct akida_dma_chan *chan)
{
	spin_lock(&wq->lock);
	chan->is_used = false;
	wake_up_locked(wq);
	spin_unlock(&wq->lock);
}

static inline struct akida_dma_chan *akida_acquire_rxchan(struct akida_dev *akida)
{
	return akida_acquire_chan(&akida->wq_rxchan, akida->rxchan,
				  ARRAY_SIZE(akida->rxchan));
}

static inline void akida_release_rxchan(struct akida_dev *akida, struct akida_dma_chan *rxchan)
{
	akida_release_chan(&akida->wq_rxchan, rxchan);
}

static inline struct akida_dma_chan *akida_acquire_txchan(struct akida_dev *akida)
{
	return akida_acquire_chan(&akida->wq_txchan, akida->txchan,
				  ARRAY_SIZE(akida->txchan));
}

static inline void akida_release_txchan(struct akida_dev *akida, struct akida_dma_chan *txchan)
{
	akida_release_chan(&akida->wq_txchan, txchan);
}

static ssize_t akida_read(struct file *file, char __user *buf,
			  size_t sz, loff_t *ppos)
{
	struct akida_dev *akida =
		container_of(file->private_data, struct akida_dev, miscdev);
	void *tmp;
	ssize_t ret;
	size_t left;
	size_t size;
	char __user *usr_buf;
	struct akida_dma_chan *rxchan;

	if (!akida_is_allowed(*ppos, sz)) {
		pci_err(akida->pdev, "dma transfer @0x%llx, %zu bytes not allowed\n",
			*ppos, sz);
		return -EINVAL;
	}

	tmp = kmalloc(AKIDA_DMA_XFER_MAX_SIZE, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	rxchan = akida_acquire_rxchan(akida);
	if (IS_ERR(rxchan)) {
		kfree(tmp);
		return PTR_ERR(rxchan);
	}

	left = sz;
	usr_buf = buf;
	while (left) {
		/* Limit transfer chunk ... */
		size = left > AKIDA_DMA_XFER_MAX_SIZE ?
			AKIDA_DMA_XFER_MAX_SIZE : left;

		/* ... do transfer ... */
		ret = akida_dma_transfer(akida, rxchan, *ppos, size, tmp);
		if (ret < 0)
			goto end;

		/* ... copy transfered chunk to the user buffer */
		if (copy_to_user(usr_buf, tmp, size)) {
			ret = -EFAULT;
			goto end;
		}

		*ppos += size;
		usr_buf += size;
		left -= size;
	}

	ret = sz;
end:
	akida_release_rxchan(akida, rxchan);
	kfree(tmp);
	return ret;
}

static ssize_t akida_write(struct file *file, const char __user *buf,
			   size_t sz, loff_t *ppos)
{
	struct akida_dev *akida =
		container_of(file->private_data, struct akida_dev, miscdev);
	void *tmp;
	ssize_t ret;
	size_t left;
	size_t size;
	const char __user *usr_buf;
	struct akida_dma_chan *txchan;

	if (!akida_is_allowed(*ppos, sz)) {
		pci_err(akida->pdev, "dma transfer @0x%llx, %zu bytes not allowed\n",
			*ppos, sz);
		return -EINVAL;
	}

	tmp = kmalloc(AKIDA_DMA_XFER_MAX_SIZE, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	txchan = akida_acquire_txchan(akida);
	if (IS_ERR(txchan)) {
		kfree(tmp);
		return PTR_ERR(txchan);
	}

	left = sz;
	usr_buf = buf;
	while (left) {
		/* Limit transfer chunk ... */
		size = left > AKIDA_DMA_XFER_MAX_SIZE ?
			AKIDA_DMA_XFER_MAX_SIZE : left;

		/* ... copy chunk from the user buffer ... */
		if (copy_from_user(tmp, usr_buf, size)) {
			ret = -EFAULT;
			goto end;
		}

		/* ... do transfer */
		ret = akida_dma_transfer(akida, txchan, *ppos, size, tmp);
		if (ret < 0)
			goto end;

		*ppos += size;
		usr_buf += size;
		left -= size;
	}

	ret = sz;

end:
	akida_release_txchan(akida, txchan);
	kfree(tmp);
	return ret;
}

static const struct vm_operations_struct akida_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int akida_mmap(struct akida_dev *akida, unsigned int bar, struct vm_area_struct *vma)
{
	vma->vm_pgoff += (pci_resource_start(akida->pdev, bar) >> PAGE_SHIFT);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &akida_vm_ops;

	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  vma->vm_end - vma->vm_start,
				  vma->vm_page_prot);

}

static int akida_1000_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct akida_dev *akida =
		container_of(file->private_data, struct akida_dev, miscdev);
	unsigned long size;

	if (!(pci_resource_flags(akida->pdev, BAR_0) & IORESOURCE_MEM))
		return -EINVAL;

	size = ((pci_resource_len(akida->pdev, BAR_0) - 1) >> PAGE_SHIFT) + 1;
	if (vma->vm_pgoff + vma_pages(vma) > size)
		return -EINVAL;

	return akida_mmap(akida, BAR_0, vma);
}

static int akida_1500_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct akida_dev *akida =
		container_of(file->private_data, struct akida_dev, miscdev);
	unsigned long start [3], size[3];
	unsigned int bar;

	start[0] = AKIDA_1500_BAR2_OFFSET >> PAGE_SHIFT;
	start[1] = AKIDA_1500_BAR4_OFFSET >> PAGE_SHIFT;
	start[2] = AKIDA_1500_HOST_DDR_BASE >> PAGE_SHIFT;
	size[0] = ((pci_resource_len(akida->pdev, BAR_2) - 1) >> PAGE_SHIFT) + 1;
	size[1] = ((pci_resource_len(akida->pdev, BAR_4) - 1) >> PAGE_SHIFT) + 1;
	size[2] = ((AKIDA_1500_HOST_DDR_SIZE - 1) >> PAGE_SHIFT) + 1;

	if (start[0] <= vma->vm_pgoff &&
	    (vma->vm_pgoff + vma_pages(vma)) <= (start[0] + size[0])) {
		bar = BAR_2;
		vma->vm_pgoff -= start[0];
	} else if (start[1] <= vma->vm_pgoff &&
		   (vma->vm_pgoff + vma_pages(vma)) <= (start[1] + size[1])) {
		bar = BAR_4;
		vma->vm_pgoff -= start[1];
	} else if (start[2] <= vma->vm_pgoff &&
		   (vma->vm_pgoff + vma_pages(vma)) <= (start[2] + size[2])) {
		vma->vm_pgoff -= start[2];
		return dma_mmap_attrs(&akida->pdev->dev, vma,
			akida->host_ddr.cpu_addr, akida->host_ddr.dma_addr,
			AKIDA_1500_HOST_DDR_BASE, AKIDA_1500_HOST_DDR_DMA_ATTRS);
	} else
		return -EINVAL;

	if (!(pci_resource_flags(akida->pdev, bar) & IORESOURCE_MEM))
		return -EINVAL;

	return akida_mmap(akida, bar, vma);
}

static const struct file_operations akida_1000_fops = {
	.owner = THIS_MODULE,
	.write = akida_write,
	.read = akida_read,
	.llseek = no_seek_end_llseek,
	.mmap = akida_1000_mmap,
};

static const struct file_operations akida_1500_fops = {
	.owner = THIS_MODULE,
	.write = akida_write,
	.read = akida_read,
	.llseek = no_seek_end_llseek,
	.mmap = akida_1500_mmap,
};

struct akida_iatu_conf {
	int addr;
	u32 val;
};

static const struct akida_iatu_conf akida_1000_iatu_conf_table[] = {
	/* Akida BAR
	 * Region 0 inbound (31:dir N-0:Region Index)
	 * Function 0 Mem   (22-20:function)
	 * Lower Target -  AKIDA CORE APB
	 * CTRL2            (31:EN, 30:BAR Match, 19:FUNC EN, 10-8:BAR0)
	 */
	{.addr = 0x0900, .val = 0x80000000},
	{.addr = 0x0904, .val = 0x00000000},
	{.addr = 0x0918, .val = 0xFCC00000},
	{.addr = 0x0908, .val = 0xC0080000},

	/* EP_iATU Region 1 Inbound Setting
	 * Region 1 inbound (31:dir N-0:Region Index)
	 * Function 0 Mem   (22-20:function)
	 * PCIe EP DBI APB System - DMA controller mapped here at offset 0x970
	 * CTRL2            (31:EN, 30:BAR Match, 19:FUNC EN, 10-8:BAR2)
	 */
	{.addr = 0x0900, .val = 0x80000001},
	{.addr = 0x0904, .val = 0x00000000},
	{.addr = 0x0918, .val = 0xF8C00000},
	{.addr = 0x0908, .val = 0xC0080200},

	/* EP_iATU Region 2 Inbound Setting
	 * Region 2 inbound (31:dir N-0:Region Index)
	 * Function 0 memory for testing DstBuffer in DDR at 4MB   (22-20:function)
	 * LPDDR APB System - used for SGL/LL (first 1MB), Data buffers (3MB).
	 * CTRL2            (31:EN, 30:BAR Match, 19:FUNC EN, 10-8:BAR4)
	 */
	{.addr = 0x0900, .val = 0x80000002},
	{.addr = 0x0904, .val = 0x00000000},
	{.addr = 0x0918, .val = AKIDA_DMA_RAM_PHY_ADDR},
	{.addr = 0x0908, .val = 0xC0080400},

	/* End of configuration */
	{0}
};

static int akida_1500_setup_host_ddr(struct akida_dev *akida)
{
	akida->host_ddr.cpu_addr = dmam_alloc_attrs(&akida->pdev->dev,
		AKIDA_1500_HOST_DDR_SIZE, &akida->host_ddr.dma_addr,
		GFP_KERNEL, AKIDA_1500_HOST_DDR_DMA_ATTRS);

	if (!akida->host_ddr.cpu_addr) {
		pci_err(akida->pdev, "Failed to allocate host ddr area\n");
		return -ENOMEM;
	}

	return 0;
}

static const struct akida_iatu_conf akida_1500_iatu_conf_table[] = {
	/* Akida BAR
	 * EP_iATU Region 1 Inbound Setting
	 * [x:20]:func [4:0]:TYPE,
	 * [31]:EN, 30:Match Mode, 28:CFG SHIFT, 19:Func Match, 10:8:BAR, 7:0:MSG
	 */
	{.addr=0x0700, .val=0x00000000},
	{.addr=0x0704, .val=0xC0080200},
	{.addr=0x0714, .val=AKIDA_1500_BAR2_OFFSET},

	/* EP_iATU Region 2 Inbound Setting
	 * [x:20]:func [4:0]:TYPE,
	 * [31]:EN, 30:Match Mode, 28:CFG SHIFT, 19:Func Match, 10:8:BAR, 7:0:MSG
	 */
	{.addr=0x0900, .val=0x00000000},
	{.addr=0x0904, .val=0xC0080400},
	{.addr=0x0914, .val=AKIDA_1500_BAR4_OFFSET},

	/* End of configuration */
	{0}
};

static int akida_1000_setup_iatu(struct akida_dev *akida)
{
	const struct akida_iatu_conf *conf = akida_1000_iatu_conf_table;
	struct pci_dev *pdev = akida->pdev;
	int ret;

	while (conf->addr) {
		ret = pci_write_config_dword(pdev, conf->addr, conf->val);
		if (ret) {
			pci_err(pdev, "write config dword 0x%x 0x%x failed (%d)\n",
				conf->addr, conf->val, ret);
			return ret;
		}
		conf++;
	}

	/* Provide 1000ms sleep for iATU's to be setup */
	msleep(1000);

	return 0;
}

static int akida_1500_setup_iatu(struct akida_dev *akida)
{
	const struct akida_iatu_conf *conf = akida_1500_iatu_conf_table;
	struct pci_dev *pdev = akida->pdev;
	int ret;

	/* Mapping PCI BAR regions:
	 *  - BAR0: IATU regs and eDMA regs
	 */
	ret = pcim_iomap_regions(pdev, BIT(BAR_0), pci_name(pdev));
	if (ret) {
		pci_err(pdev, "BAR I/O remapping failed (%d)\n", ret);
		return ret;
	}
	akida->mmio_bar0 = pcim_iomap_table(pdev)[BAR_0];

	while (conf->addr) {
		writel(conf->val, akida->mmio_bar0 + conf->addr);
		conf++;
	}

	/* Host DDR
	 * EP_iATU Region 0 Outbound Setting
	 */
	writel(0x00000000, akida->mmio_bar0 + 0x404);
	writel(0x00000000, akida->mmio_bar0 + 0x400);
	writel(AKIDA_1500_HOST_DDR_BASE, akida->mmio_bar0 + 0x408);
	writel(0x00000000, akida->mmio_bar0 + 0x40c);
	writel(AKIDA_1500_HOST_DDR_BASE + AKIDA_1500_HOST_DDR_SIZE - 1, akida->mmio_bar0 + 0x410);
	writel(lower_32_bits(akida->host_ddr.dma_addr), akida->mmio_bar0 + 0x414);
	writel(upper_32_bits(akida->host_ddr.dma_addr), akida->mmio_bar0 + 0x418);
	writel(0x80000000, akida->mmio_bar0 + 0x404);

	/* Provide 1000ms sleep for iATU's to be setup */
	msleep(1000);

	return 0;
}

static int akida_1000_setup_iomap(struct pci_dev *pdev)
{
	/* PCI BAR regions:
	 *  - BAR2: eDMA regs,
	 *  - BAR4: eDMA linked-list and data
	 */
	return pcim_iomap_regions(pdev, BIT(BAR_2) | BIT(BAR_4), pci_name(pdev));
}

static int akida_1500_setup_iomap(struct pci_dev *pdev)
{
	/* PCI BAR regions:
	 *  - BAR4: eDMA linked-list and data
	 */
	return pcim_iomap_regions(pdev, BIT(BAR_4), pci_name(pdev));
}


static void akida_1000_setup_dma_reg_base(struct akida_dev *akida)
{
	akida->edma_chip.reg_base = pcim_iomap_table(akida->pdev)[BAR_2];
	akida->edma_chip.reg_base += 0x00000970;
}

static void akida_1500_setup_dma_reg_base(struct akida_dev *akida)
{
	akida->edma_chip.reg_base = pcim_iomap_table(akida->pdev)[BAR_0];
}

struct akida_filter_param {
	struct akida_dev *akida;
	u32 dir_mask;
	u32 dir_exp;
};

static bool akida_dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct akida_filter_param *p = param;
	struct dma_slave_caps caps;
	int ret;

	/* Check that the engine used is the one we have instanciate */
	if (&p->akida->pdev->dev != chan->device->dev)
		return false;

	/* Get capabilities and check expected direction */
	ret = dma_get_slave_caps(chan, &caps);
	if (ret)
		return false;

	if ((caps.directions & p->dir_mask) != p->dir_exp)
		return false;

	return true;
}

static int akida_dma_init(struct akida_dev *akida)
{
	struct akida_filter_param p;
	dma_cap_mask_t mask;
	unsigned int i;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	p.akida = akida;
	p.dir_mask = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);

	/* 1. Init rx channels */
	for (i = 0; i < ARRAY_SIZE(akida->rxchan); i++) {
		p.dir_exp = BIT(DMA_DEV_TO_MEM);
		akida->rxchan[i].chan = dma_request_channel(mask,
						akida_dma_chan_filter, &p);
		if (!akida->rxchan[i].chan) {
			pci_err(akida->pdev, "Request DMA rxchan[%u] fails\n",
				i);
			goto free_rxchan;
		}
		module_put(akida->edma_chip.dev->driver->owner);

		init_completion(&akida->rxchan[i].dma_complete);

		akida->rxchan[i].dma_xfer_dir = DMA_DEV_TO_MEM;
		akida->rxchan[i].dma_data_dir = DMA_FROM_DEVICE;
	}


	/* 2. Init tx channels */
	for (i = 0; i < ARRAY_SIZE(akida->txchan); i++) {
		p.dir_exp = BIT(DMA_MEM_TO_DEV);
		akida->txchan[i].chan = dma_request_channel(mask,
						akida_dma_chan_filter, &p);
		if (!akida->txchan[i].chan)  {
			pci_err(akida->pdev, "Request DMA txchan[%u] fails\n",
				i);
			goto free_txchan;
		}
		module_put(akida->edma_chip.dev->driver->owner);

		init_completion(&akida->txchan[i].dma_complete);

		akida->txchan[i].dma_xfer_dir = DMA_MEM_TO_DEV;
		akida->txchan[i].dma_data_dir = DMA_TO_DEVICE;
	}

	return 0;

free_txchan:
	for (i = 0; i < ARRAY_SIZE(akida->txchan); i++) {
		if (akida->txchan[i].chan) {
			__module_get(akida->edma_chip.dev->driver->owner);
			dma_release_channel(akida->txchan[i].chan);
		}
	}
free_rxchan:
	for (i = 0; i < ARRAY_SIZE(akida->rxchan); i++) {
		if (akida->rxchan[i].chan) {
			__module_get(akida->edma_chip.dev->driver->owner);
			dma_release_channel(akida->rxchan[i].chan);
		}
	}
	return -EBUSY;
}

static void akida_dma_exit(struct akida_dev *akida)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(akida->txchan); i++) {
		dmaengine_terminate_sync(akida->txchan[i].chan);
		__module_get(akida->edma_chip.dev->driver->owner);
		dma_release_channel(akida->txchan[i].chan);
	}

	for (i = 0; i < ARRAY_SIZE(akida->rxchan); i++) {
		dmaengine_terminate_sync(akida->rxchan[i].chan);
		__module_get(akida->edma_chip.dev->driver->owner);
		dma_release_channel(akida->rxchan[i].chan);
	}
}

static int akida_dw_edma_pcie_irq_vector(struct device *dev, unsigned int nr)
{
	return pci_irq_vector(to_pci_dev(dev), nr);
}

static const struct dw_edma_plat_ops akida_dw_edma_plat_ops = {
	.irq_vector = akida_dw_edma_pcie_irq_vector,
};

struct akida_ops {
	char miscdev_name[10];
	int (*setup_host_ddr)(struct akida_dev *akida);
	int (*setup_iatu)(struct akida_dev *akida);
	int (*setup_iomap)(struct pci_dev *pdev);
	void (*setup_dma_reg_base)(struct akida_dev *akida);
	const struct file_operations *fops;
	enum dw_edma_map_format mf;
};

static struct akida_ops akida_1000_ops = {
	.miscdev_name = "akida",
	.setup_iatu = akida_1000_setup_iatu,
	.setup_iomap = akida_1000_setup_iomap,
	.setup_dma_reg_base = akida_1000_setup_dma_reg_base,
	.fops = &akida_1000_fops,
	.mf = EDMA_MF_EDMA_LEGACY,
};

static struct akida_ops akida_1500_ops = {
	.miscdev_name = "akd1500_",
	.setup_host_ddr = akida_1500_setup_host_ddr,
	.setup_iatu = akida_1500_setup_iatu,
	.setup_iomap = akida_1500_setup_iomap,
	.setup_dma_reg_base = akida_1500_setup_dma_reg_base,
	.fops = &akida_1500_fops,
	.mf = EDMA_MF_HDMA_NATIVE,
};

static int akida_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int board_id = id->driver_data;
	struct akida_dev *akida;
	struct akida_ops ops;
	int ret, nr_irqs;

	switch (board_id) {
	case AKIDA_1000:
		ops = akida_1000_ops;
		break;
	case AKIDA_1500:
		ops = akida_1500_ops;
		break;
	default:
		return -EOPNOTSUPP;
	}

	akida = devm_kzalloc(&pdev->dev, sizeof(*akida), GFP_KERNEL);
	if (!akida)
		return -ENOMEM;

	akida->pdev = pdev;

	/* Disable ASPM L0s and L1 states as they cause device stop working */
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);

	/* Enable PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "enabling device failed (%d)\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	/* DMA configuration */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 17, 0)
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
#else
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
#endif
	if (!ret) {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 17, 0)
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
#else
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
#endif
		if (ret) {
			pci_err(pdev, "consistent DMA mask 64 set failed (%d)\n", ret);
			return ret;
		}
	} else {
		pci_warn(pdev, "DMA mask 64 set failed\n");

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 17, 0)
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
#else
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
#endif
		if (ret) {
			pci_err(pdev, "DMA mask 32 set failed (%d)\n", ret);
			return ret;
		}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 17, 0)
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
#else
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
#endif
		if (ret) {
			pci_err(pdev, "consistent DMA mask 32 set failed (%d)\n", ret);
			return ret;
		}
	}

	/* Setup host DDR area */
	if (ops.setup_host_ddr) {
		ret = ops.setup_host_ddr(akida);
		if (ret) {
			pci_err(pdev, "seting up host ddr area failed (%d)\n", ret);
			return ret;
		}
	}

	/* Setup iATU */
	ret = ops.setup_iatu(akida);
	if (ret) {
		pci_err(pdev, "seting up iATU failed (%d)\n", ret);
		return ret;
	}

	/* Mapping PCI BAR regions */
	ret = ops.setup_iomap(pdev);
	if (ret) {
		pci_err(pdev, "BAR I/O remapping failed (%d)\n", ret);
		return ret;
	}

	/* IRQs allocation */
	nr_irqs = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (nr_irqs < 1) {
		pci_err(pdev, "fail to alloc IRQ vector (%d)\n",
			nr_irqs);
		return -EPERM;
	}

	/* Checked if PCI interrupts were enabled */
	if (!pci_dev_msi_enabled(pdev)) {
		pci_err(pdev, "enable interrupt failed\n");
		ret = -EPERM;
		goto fail_free_irq_vectors;
	}

	/* Setup eDMA engine */
	ops.setup_dma_reg_base(akida);

	/* 2 write and 2 read channels */
	akida->edma_chip.ll_wr_cnt = 2;
	akida->edma_chip.ll_rd_cnt = 2;

	/* BAR4 maps to 0x2000000 from DMA controller point of view
	 * This area contains linked-list and data buffers (1MB + 3MB)
	 */
	akida->edma_chip.ll_region_wr[0].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.ll_region_wr[0].vaddr.io += AKIDA_DMA_RAM_PHY_LL_OFFSET(TX,0);
	akida->edma_chip.ll_region_wr[0].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.ll_region_wr[0].paddr += AKIDA_DMA_RAM_PHY_LL_OFFSET(TX,0);
	akida->edma_chip.ll_region_wr[0].sz = AKIDA_DMA_RAM_PHY_LL_SIZE(TX,0);

	akida->edma_chip.dt_region_wr[0].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.dt_region_wr[0].vaddr.io += AKIDA_DMA_RAM_PHY_DT_OFFSET(TX,0);
	akida->edma_chip.dt_region_wr[0].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.dt_region_wr[0].paddr += AKIDA_DMA_RAM_PHY_DT_OFFSET(TX,0);
	akida->edma_chip.dt_region_wr[0].sz = AKIDA_DMA_RAM_PHY_DT_SIZE(TX,0);

	akida->edma_chip.ll_region_wr[1].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.ll_region_wr[1].vaddr.io += AKIDA_DMA_RAM_PHY_LL_OFFSET(TX,1);
	akida->edma_chip.ll_region_wr[1].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.ll_region_wr[1].paddr += AKIDA_DMA_RAM_PHY_LL_OFFSET(TX,1);
	akida->edma_chip.ll_region_wr[1].sz = AKIDA_DMA_RAM_PHY_LL_SIZE(TX,1);

	akida->edma_chip.dt_region_wr[1].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.dt_region_wr[1].vaddr.io += AKIDA_DMA_RAM_PHY_DT_OFFSET(TX,1);
	akida->edma_chip.dt_region_wr[1].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.dt_region_wr[1].paddr += AKIDA_DMA_RAM_PHY_DT_OFFSET(TX,1);
	akida->edma_chip.dt_region_wr[1].sz = AKIDA_DMA_RAM_PHY_DT_SIZE(TX,1);

	akida->edma_chip.ll_region_rd[0].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.ll_region_rd[0].vaddr.io += AKIDA_DMA_RAM_PHY_LL_OFFSET(RX,0);
	akida->edma_chip.ll_region_rd[0].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.ll_region_rd[0].paddr += AKIDA_DMA_RAM_PHY_LL_OFFSET(RX,0);
	akida->edma_chip.ll_region_rd[0].sz = AKIDA_DMA_RAM_PHY_LL_SIZE(RX,0);

	akida->edma_chip.dt_region_rd[0].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.dt_region_rd[0].vaddr.io += AKIDA_DMA_RAM_PHY_DT_OFFSET(RX,0);
	akida->edma_chip.dt_region_rd[0].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.dt_region_rd[0].paddr += AKIDA_DMA_RAM_PHY_DT_OFFSET(RX,0);
	akida->edma_chip.dt_region_rd[0].sz = AKIDA_DMA_RAM_PHY_DT_SIZE(RX,0);

	akida->edma_chip.ll_region_rd[1].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.ll_region_rd[1].vaddr.io += AKIDA_DMA_RAM_PHY_LL_OFFSET(RX,1);
	akida->edma_chip.ll_region_rd[1].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.ll_region_rd[1].paddr += AKIDA_DMA_RAM_PHY_LL_OFFSET(RX,1);
	akida->edma_chip.ll_region_rd[1].sz = AKIDA_DMA_RAM_PHY_LL_SIZE(RX,1);

	akida->edma_chip.dt_region_rd[1].vaddr.io = pcim_iomap_table(pdev)[BAR_4];
	akida->edma_chip.dt_region_rd[1].vaddr.io += AKIDA_DMA_RAM_PHY_DT_OFFSET(RX,1);
	akida->edma_chip.dt_region_rd[1].paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->edma_chip.dt_region_rd[1].paddr += AKIDA_DMA_RAM_PHY_DT_OFFSET(RX,1);
	akida->edma_chip.dt_region_rd[1].sz = AKIDA_DMA_RAM_PHY_DT_SIZE(RX,1);

	akida->edma_chip.mf = ops.mf;
	akida->edma_chip.nr_irqs = 1;
	akida->edma_chip.ops = &akida_dw_edma_plat_ops;

	/* Debug info */
	switch (akida->edma_chip.mf) {
	case EDMA_MF_EDMA_LEGACY:
		pci_dbg(pdev, "Version: eDMA Port Logic (0x%x)\n", akida->edma_chip.mf);
		break;
	case EDMA_MF_EDMA_UNROLL:
		pci_dbg(pdev, "Version: eDMA Unroll (0x%x)\n", akida->edma_chip.mf);
		break;
	case EDMA_MF_HDMA_COMPAT:
		pci_dbg(pdev, "Version: HDMA Compatible (0x%x)\n", akida->edma_chip.mf);
		break;
	case EDMA_MF_HDMA_NATIVE:
		pci_dbg(pdev, "Version: HDMA Native (0x%x)\n", akida->edma_chip.mf);
		break;
	default:
		pci_dbg(pdev, "Version:\tUnknown (0x%x)\n", akida->edma_chip.mf);
		break;
	}
	pci_dbg(pdev, "Registers: addr(v=%p)\n",
		akida->edma_chip.reg_base);
	pci_dbg(pdev, "Wr[0] LL:   addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.ll_region_wr[0].vaddr.io, &akida->edma_chip.ll_region_wr[0].paddr,
		akida->edma_chip.ll_region_wr[0].sz);
	pci_dbg(pdev, "Wr[0] Data: addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.dt_region_wr[0].vaddr.io, &akida->edma_chip.dt_region_wr[0].paddr,
		akida->edma_chip.dt_region_wr[0].sz);
	pci_dbg(pdev, "Wr[1] LL:   addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.ll_region_wr[1].vaddr.io, &akida->edma_chip.ll_region_wr[1].paddr,
		akida->edma_chip.ll_region_wr[1].sz);
	pci_dbg(pdev, "Wr[1] Data: addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.dt_region_wr[1].vaddr.io, &akida->edma_chip.dt_region_wr[1].paddr,
		akida->edma_chip.dt_region_wr[1].sz);
	pci_dbg(pdev, "Rd[0] LL:   addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.ll_region_rd[0].vaddr.io, &akida->edma_chip.ll_region_rd[0].paddr,
		akida->edma_chip.ll_region_rd[0].sz);
	pci_dbg(pdev, "Rd[0] Data: addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.dt_region_rd[0].vaddr.io, &akida->edma_chip.dt_region_rd[0].paddr,
		akida->edma_chip.dt_region_rd[0].sz);
	pci_dbg(pdev, "Rd[1] LL:   addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.ll_region_rd[1].vaddr.io, &akida->edma_chip.ll_region_rd[1].paddr,
		akida->edma_chip.ll_region_rd[1].sz);
	pci_dbg(pdev, "Rd[1] Data: addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->edma_chip.dt_region_rd[1].vaddr.io, &akida->edma_chip.dt_region_rd[1].paddr,
		akida->edma_chip.dt_region_rd[1].sz);
	pci_dbg(pdev, "Nr. IRQs: %u\n", akida->edma_chip.nr_irqs);

	akida->edma_chip.dev = &pdev->dev;

	/* Starting eDMA driver */
	ret = akida_dw_edma_probe(&akida->edma_chip);
	if (ret) {
		pci_err(pdev, "eDMA probe failed (%d)\n", ret);
		goto fail_free_irq_vectors;
	}

	/* Init dma */
	ret = akida_dma_init(akida);
	if (ret) {
		pci_err(pdev, "eDMA probe failed (%d)\n", ret);
		goto fail_dw_edma_remove;
	}

	/* Init waitqueues */
	init_waitqueue_head(&akida->wq_rxchan);
	init_waitqueue_head(&akida->wq_txchan);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0)
	ret = ida_simple_get(&akida_devno, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		pci_err(pdev, "ida simple get failed (%d)\n", ret);
		goto fail_akida_dma_exit;
	}
#else
	ret = ida_alloc(&akida_devno, GFP_KERNEL);
	if (ret < 0) {
		pci_err(pdev, "ida alloc failed (%d)\n", ret);
		goto fail_akida_dma_exit;
	}
#endif
	akida->devno = ret;

	/* Declare misc device */
	akida->miscdev.minor = MISC_DYNAMIC_MINOR;
	akida->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
					     "%s%d", ops.miscdev_name,
					     akida->devno);
	akida->miscdev.fops = ops.fops;
	akida->miscdev.parent = &pdev->dev;

	pci_set_drvdata(pdev, akida);

	ret = misc_register(&akida->miscdev);
	if (ret) {
		pci_err(pdev, "Cannot register misc device (%d)\n", ret);
		goto fail_ida_alloc;
	}

	pci_info(pdev, "probed (%s)\n", akida->miscdev.name);

	return 0;

fail_ida_alloc:
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0)
	ida_simple_remove(&akida_devno, akida->devno);
#else
	ida_free(&akida_devno, akida->devno);
#endif
fail_akida_dma_exit:
	akida_dma_exit(akida);
fail_dw_edma_remove:
	akida_dw_edma_remove(&akida->edma_chip);
fail_free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return ret;
}

static void akida_remove(struct pci_dev *pdev)
{
	struct akida_dev *akida = pci_get_drvdata(pdev);
	int ret;

	misc_deregister(&akida->miscdev);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0)
	ida_simple_remove(&akida_devno, akida->devno);
#else
	ida_free(&akida_devno, akida->devno);
#endif
	if (akida->txchan[0].chan && akida->rxchan[0].chan)
		akida_dma_exit(akida);
	if (akida->edma_chip.dev) {
		ret = akida_dw_edma_remove(&akida->edma_chip);
		if (ret)
			pci_warn(pdev, "can't remove device properly (%d)\n", ret);
	}
	if (pci_dev_msi_enabled(pdev))
		pci_free_irq_vectors(pdev);

	if (akida->mmio_bar0)
		pcim_iounmap_regions(pdev, BIT(BAR_0));

	pci_info(pdev, "removed\n");
}

#ifndef PCI_VENDOR_ID_BRAINCHIP
#define PCI_VENDOR_ID_BRAINCHIP 0x1e7c
#endif

#ifndef PCI_DEVICE_ID_BRAINCHIP_AKIDA_1000
#define PCI_DEVICE_ID_BRAINCHIP_AKIDA_1000 0xbca1
#endif

#ifndef PCI_DEVICE_ID_BRAINCHIP_AKIDA_1500
#define PCI_DEVICE_ID_BRAINCHIP_AKIDA_1500 0xa500
#endif

static const struct pci_device_id akida_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BRAINCHIP, PCI_DEVICE_ID_BRAINCHIP_AKIDA_1000), 0, 0, AKIDA_1000},
	{ PCI_DEVICE(PCI_VENDOR_ID_BRAINCHIP, PCI_DEVICE_ID_BRAINCHIP_AKIDA_1500), 0, 0, AKIDA_1500},
	{}
};

MODULE_DEVICE_TABLE(pci, akida_pci_ids);

static struct pci_driver akida_driver = {
	.name		= "akida-pcie",
	.id_table	= akida_pci_ids,
	.probe		= akida_probe,
	.remove		= akida_remove,
};

module_pci_driver(akida_driver);

MODULE_DESCRIPTION("Brainchip Akida PCIe");
MODULE_LICENSE("GPL");
