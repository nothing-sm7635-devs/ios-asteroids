#pragma once
#include <stdint.h>

/* GPI DMA for QUP0 SE0 SPI: GPII 0 (static), TX channel 0, RX channel 1. */
int gpi_init(void);
/* Full-duplex SPI frame of n bytes (n <= GPI_BUF), CS held for the frame. */
int gpi_spi_xfer(const uint8_t *tx, uint8_t *rx, unsigned n);
/* (events seen << 16) | TX completion code << 8 | RX code, last transfer */
uint32_t gpi_status(void);
/* Print sCR0 and the SMR/S2CR matching GPI, UFS and MDP stream IDs. */
void gpi_smmu_check(void);
/* Route SID 0x576 to UFS's S2CR in a free SMR; 0 if it reads back. */
int gpi_smmu_route(void);
/* Doorbell in steps (no TRE, one NOP TRE), pausing before each; 0 if the NOP completed. */
int gpi_stage_test(void);
/* Print GPII / channel state (after a failure). */
void gpi_dump(void);

#define GPI_BUF		128
