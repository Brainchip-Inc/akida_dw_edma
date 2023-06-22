/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Brainchip.
 * Akida dw-edma wrapper
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#ifndef _AKIDA_DW_EDMA_H
#define _AKIDA_DW_EDMA_H

#include <linux/dma/edma.h>

int akida_dw_edma_probe(struct dw_edma_chip *chip);
int akida_dw_edma_remove(struct dw_edma_chip *chip);

#endif /* _AKIDA_DW_EDMA_H */
