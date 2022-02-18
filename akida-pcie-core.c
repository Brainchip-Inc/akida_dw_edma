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

#include "dw-edma-core.h"

static DEFINE_IDA(akida_devno);

/* The DMA RAM area contains eDMA linked-list (LL) and data (DT).
 * This area is used by the eDMA controler and is located inside the device.
 * This physical address is from the eDMA point of view
 */
#define AKIDA_DMA_RAM_PHY_ADDR	0x20000000
#define AKIDA_DMA_RAM_PHY_LL_OFFSET	0x00000000
#define AKIDA_DMA_RAM_PHY_LL_SIZE	0x00100000 /* 1MB */
#define AKIDA_DMA_RAM_PHY_DT_OFFSET	0x00100000
#define AKIDA_DMA_RAM_PHY_DT_SIZE	0x00300000 /* 3MB */
#define AKIDA_DMA_RAM_PHY_SIZE	0x00400000 /* 4MB */

struct akida_dev {
	struct pci_dev *pdev;
	int devno;
	struct miscdevice miscdev;
	struct dw_edma_chip edma_chip;
	struct dw_edma dw;
	struct dma_chan *rxchan;
	struct dma_chan *txchan;
	struct dma_chan *dma_chan_using;
	struct completion dma_complete;
	enum dma_data_direction dma_data_dir;
	dma_addr_t dma_buf;
	size_t dma_len;
};

static void akida_dma_callback(void *arg)
{
	struct akida_dev *akida = arg;

	dma_unmap_single(akida->dma_chan_using->device->dev,
		akida->dma_buf, akida->dma_len, akida->dma_data_dir);
	complete(&akida->dma_complete);
}

static int akida_dma_transfer(struct akida_dev *akida,
	enum dma_transfer_direction direction, phys_addr_t dev_addr,
	size_t len, void *buf)
{
	struct dma_slave_config dma_sconfig = {0};
	struct dma_async_tx_descriptor *txdesc;
	struct device *chan_dev;

	int ret;

	/* Set parameters */
	dma_sconfig.direction = direction;
	switch (direction) {
	case DMA_MEM_TO_DEV:
		dma_sconfig.dst_addr = dev_addr;
		dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		akida->dma_chan_using = akida->txchan;
		akida->dma_data_dir = DMA_TO_DEVICE;
		break;
	case DMA_DEV_TO_MEM:
		dma_sconfig.src_addr = dev_addr;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		akida->dma_chan_using = akida->rxchan;
		akida->dma_data_dir = DMA_FROM_DEVICE;
		break;
	default:
		pci_err(akida->pdev, "Unsupported direction (%d)\n", direction);
		return -EINVAL;
	}

	/* Map buffer */
	akida->dma_len = len;
	chan_dev = akida->dma_chan_using->device->dev;
	akida->dma_buf = dma_map_single(chan_dev, buf, akida->dma_len,
				        akida->dma_data_dir);
	if (dma_mapping_error(chan_dev, akida->dma_buf)) {
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
	if (direction == DMA_MEM_TO_DEV)
		dma_sconfig.src_addr = akida->dma_buf;
	else
		dma_sconfig.dst_addr = akida->dma_buf;

	ret = dmaengine_slave_config(akida->dma_chan_using, &dma_sconfig);
	if (ret < 0) {
		pci_err(akida->pdev, "DMA slave config failed (%d)\n", ret);
		goto err;
	}

	/* Prepare transaction */
	txdesc = dmaengine_prep_slave_single(akida->dma_chan_using,
					     akida->dma_buf, akida->dma_len,
					     direction, DMA_PREP_INTERRUPT);
	if (!txdesc) {
		pci_err(akida->pdev, "Not able to get desc for DMA xfer\n");
		ret = -EINVAL;
		goto err;
	}

	/* Clear completion */
	reinit_completion(&akida->dma_complete);

	/* Submit transaction */
	txdesc->callback = akida_dma_callback;
	txdesc->callback_param = akida;
	ret = dma_submit_error(dmaengine_submit(txdesc));
	if (ret < 0) {
		pci_err(akida->pdev, "DMA submit failed\n");
		goto err;
	}

	/* Start transactions */
	dma_async_issue_pending(akida->dma_chan_using);

	/* Wait for completion */
	ret = wait_for_completion_timeout(&akida->dma_complete,
					  msecs_to_jiffies(2000));
	if (!ret) {
		pci_err(akida->pdev, "DMA wait completion timed out\n");
		dmaengine_terminate_all(akida->dma_chan_using);
		ret = -ETIMEDOUT;
		goto err;
	}

	/* Ok, everything is done (unmap done in dma transaction callback) */
	return 0;

err:
	dma_unmap_single(chan_dev, akida->dma_buf, akida->dma_len,
			 akida->dma_data_dir);
	return ret;
}

static bool akida_is_allowed(phys_addr_t addr, size_t size)
{
	/* Overlap with DMA RAM reserved area is not allowed */
	return (addr+size) < AKIDA_DMA_RAM_PHY_ADDR ||
		addr >= (AKIDA_DMA_RAM_PHY_ADDR + AKIDA_DMA_RAM_PHY_SIZE);
}

static ssize_t akida_read(struct file *file, char __user *buf,
			  size_t sz, loff_t *ppos)
{
	struct akida_dev *akida =
		container_of(file->private_data, struct akida_dev, miscdev);
	void *tmp;
	ssize_t ret;

	if (!akida_is_allowed(*ppos, sz)) {
		pci_err(akida->pdev, "dma transfer @0x%llx, %zu bytes not allowed\n",
			*ppos, sz);
		return -EINVAL;
	}

	tmp = kmalloc(sz, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	ret = akida_dma_transfer(akida, DMA_DEV_TO_MEM, *ppos, sz, tmp);
	if (ret < 0)
		goto end;

	if (copy_to_user(buf, tmp, sz)) {
		ret = -EFAULT;
		goto end;
	}

	*ppos += sz;
	ret = sz;

end:
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

	if (!akida_is_allowed(*ppos, sz)) {
		pci_err(akida->pdev, "dma transfer @0x%llx, %zu bytes not allowed\n",
			*ppos, sz);
		return -EINVAL;
	}

	tmp = memdup_user(buf, sz);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	ret = akida_dma_transfer(akida, DMA_MEM_TO_DEV, *ppos, sz, tmp);
	if (ret < 0)
		goto end;

	*ppos += sz;
	ret = sz;

end:
	kfree(tmp);
	return ret;
}

static const struct file_operations akida_fops = {
	.owner = THIS_MODULE,
	.write = akida_write,
	.read = akida_read,
	.llseek = no_seek_end_llseek,
};

struct akida_iatu_conf {
	int addr;
	u32 val;
};

static const struct akida_iatu_conf akida_iatu_conf_table[] = {
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

static int akida_setup_iatu(struct pci_dev *pdev)
{
	const struct akida_iatu_conf *conf = akida_iatu_conf_table;
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

	/* Provide 100ms sleep for iATU's to be setup */
	msleep(1000);

	return 0;
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

	/* Check the the engine used is the one we have instanciate */
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

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	p.akida = akida;
	p.dir_mask = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);

	/* 1. Init rx channel */
	p.dir_exp = BIT(DMA_DEV_TO_MEM);
	akida->rxchan = dma_request_channel(mask, akida_dma_chan_filter, &p);
	if (!akida->rxchan) {
		pci_err(akida->pdev, "Request DMA rxchan fails\n");
		goto err_exit;
	}
	module_put(akida->edma_chip.dev->driver->owner);

	/* 2. Init tx channel */
	p.dir_exp = BIT(DMA_MEM_TO_DEV);
	akida->txchan = dma_request_channel(mask, akida_dma_chan_filter, &p);
	if (!akida->txchan)  {
		pci_err(akida->pdev, "Request DMA txchan fails\n");
		goto free_rxchan;
	}
	module_put(akida->edma_chip.dev->driver->owner);

	init_completion(&akida->dma_complete);

	return 0;

free_rxchan:
	dma_release_channel(akida->rxchan);
err_exit:
	return -EBUSY;
}

static void akida_dma_exit(struct akida_dev *akida)
{
	dmaengine_terminate_sync(akida->txchan);
	__module_get(akida->edma_chip.dev->driver->owner);
	dma_release_channel(akida->txchan);

	dmaengine_terminate_sync(akida->rxchan);
	__module_get(akida->edma_chip.dev->driver->owner);
	dma_release_channel(akida->rxchan);
}

static int akida_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct akida_dev *akida;
	int ret, nr_irqs;

	akida = devm_kzalloc(&pdev->dev, sizeof(*akida), GFP_KERNEL);
	if (!akida)
		return -ENOMEM;

	akida->pdev = pdev;

	/* Setup iATU */
	ret = akida_setup_iatu(pdev);
	if (ret) {
		pci_err(pdev, "seting up iATU failed (%d)\n", ret);
		return ret;
	}

	/* Enable PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "enabling device failed (%d)\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	/* Mapping PCI BAR regions:
	 *  - BAR2: eDMA regs,
	 *  - BAR4: eDMA linked-list and data
	 */
	ret = pcim_iomap_regions(pdev, BIT(BAR_2) | BIT(BAR_4), pci_name(pdev));
	if (ret) {
		pci_err(pdev, "BAR I/O remapping failed (%d)\n", ret);
		return ret;
	}

	/* DMA configuration */
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!ret) {
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (ret) {
			pci_err(pdev, "consistent DMA mask 64 set failed (%d)\n", ret);
			return ret;
		}
	} else {
		pci_warn(pdev, "DMA mask 64 set failed\n");

		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			pci_err(pdev, "DMA mask 32 set failed (%d)\n", ret);
			return ret;
		}

		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			pci_err(pdev, "consistent DMA mask 32 set failed (%d)\n", ret);
			return ret;
		}
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
	akida->dw.rg_region.vaddr = pcim_iomap_table(pdev)[BAR_2];
	akida->dw.rg_region.vaddr += 0x00000970;
	akida->dw.rg_region.paddr = pdev->resource[BAR_2].start;
	akida->dw.rg_region.paddr += 0x00000970;

	/* BAR4 maps to 0x2000000 from DMA controller point of view
	 * This area contains linked-list and data buffers (1MB + 3MB)
	 */
	akida->dw.ll_region.vaddr = pcim_iomap_table(pdev)[BAR_4];
	akida->dw.ll_region.vaddr += AKIDA_DMA_RAM_PHY_LL_OFFSET;
	akida->dw.ll_region.paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->dw.ll_region.paddr += AKIDA_DMA_RAM_PHY_LL_OFFSET;
	akida->dw.ll_region.sz = AKIDA_DMA_RAM_PHY_LL_SIZE;

	akida->dw.dt_region.vaddr = pcim_iomap_table(pdev)[BAR_4];
	akida->dw.dt_region.vaddr += AKIDA_DMA_RAM_PHY_DT_OFFSET;
	akida->dw.dt_region.paddr = AKIDA_DMA_RAM_PHY_ADDR;
	akida->dw.dt_region.paddr += AKIDA_DMA_RAM_PHY_DT_OFFSET;
	akida->dw.dt_region.sz = AKIDA_DMA_RAM_PHY_DT_SIZE;

	akida->dw.version = 0;
	akida->dw.mode = EDMA_MODE_LEGACY;
	akida->dw.nr_irqs = 1;

	akida->dw.irq = devm_kcalloc(&pdev->dev, nr_irqs,
				     sizeof(*akida->dw.irq), GFP_KERNEL);
	if (!akida->dw.irq) {
		ret = -ENOMEM;
		goto fail_free_irq_vectors;
	}

	/* Debug info */
	pci_dbg(pdev, "Version: %u\n", akida->dw.version);
	pci_dbg(pdev, "Mode: %s\n",
		akida->dw.mode == EDMA_MODE_LEGACY ? "Legacy" : "Unroll");
	pci_dbg(pdev, "Registers: addr(v=%p, p=%pa)\n",
		akida->dw.rg_region.vaddr, &akida->dw.rg_region.paddr);
	pci_dbg(pdev, "LL:   addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->dw.ll_region.vaddr, &akida->dw.ll_region.paddr,
		akida->dw.ll_region.sz);
	pci_dbg(pdev, "Data: addr(v=%p, p=%pa), sz=0x%zx bytes\n",
		akida->dw.dt_region.vaddr, &akida->dw.dt_region.paddr,
		akida->dw.dt_region.sz);
	pci_dbg(pdev, "Nr. IRQs: %u\n", akida->dw.nr_irqs);

	akida->edma_chip.dw = &akida->dw;
	akida->edma_chip.dev = &pdev->dev;
	akida->edma_chip.id = pdev->devfn;
	akida->edma_chip.irq = pdev->irq;

	/* Starting eDMA driver */
	ret = dw_edma_probe(&akida->edma_chip);
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

	ret = ida_alloc(&akida_devno, GFP_KERNEL);
	if (ret < 0) {
		pci_err(pdev, "ida alloc failed (%d)\n", ret);
		goto fail_akida_dma_exit;
	}
	akida->devno = ret;

	/* Declare misc device */
	akida->miscdev.minor = MISC_DYNAMIC_MINOR;
	akida->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
					     "akida%d", akida->devno);
	akida->miscdev.fops = &akida_fops;
	akida->miscdev.parent = &pdev->dev;

	pci_set_drvdata(pdev, akida);

	ret = misc_register(&akida->miscdev);
	if (ret) {
		pci_err(pdev, "Cannot register misc device (%d)\n", ret);
		goto fail_ida_alloc;
	}

	pci_info(pdev,"probed (%s)\n", akida->miscdev.name);

	return 0;

fail_ida_alloc:
	ida_free(&akida_devno, akida->devno);
fail_akida_dma_exit:
	akida_dma_exit(akida);
fail_dw_edma_remove:
	dw_edma_remove(&akida->edma_chip);
fail_free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return ret;
}

static void akida_remove(struct pci_dev *pdev)
{
	struct akida_dev *akida = pci_get_drvdata(pdev);
	int ret;

	misc_deregister(&akida->miscdev);
	ida_free(&akida_devno, akida->devno);
	akida_dma_exit(akida);
	ret = dw_edma_remove(&akida->edma_chip);
	if (ret)
		pci_warn(pdev, "can't remove device properly (%d)\n", ret);
	pci_free_irq_vectors(pdev);


	pci_info(pdev,"removed\n");
	return;
}

#ifndef PCI_VENDOR_ID_BRAINCHIP
#define PCI_VENDOR_ID_BRAINCHIP 0x1e7c
#endif

#ifndef PCI_DEVICE_ID_BRAINCHIP_AKIDA
#define PCI_DEVICE_ID_BRAINCHIP_AKIDA 0xbca1
#endif

static const struct pci_device_id akida_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BRAINCHIP, PCI_DEVICE_ID_BRAINCHIP_AKIDA) },
	{ 0 }
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
