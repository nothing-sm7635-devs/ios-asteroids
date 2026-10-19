#pragma once
#include <stdint.h>

/* GPI DMA for QUP0 SE0 SPI: GPII 0 (static), TX channel 0, RX channel 1. */
int gpi_init(void);
/* Full-duplex SPI frame of n bytes (n <= GPI_BUF), CS held for the frame. */
int gpi_spi_xfer(const uint8_t *tx, uint8_t *rx, unsigned n);
/* (events seen << 16) | TX completion code << 8 | RX code, last transfer */
uint32_t gpi_status(void);
/* Print GPII / channel state (after a failure). */
void gpi_dump(void);

#define GPI_BUF		128
