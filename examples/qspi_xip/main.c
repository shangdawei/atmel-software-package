/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2016, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/**
 * \page qspi_xip QSPI XIP Example
 *
 * \section Purpose
 *
 * This example demonstrates how to setup the QSPI Flash in XIP mode to execute
 * code from QSPI flash.
 *
 * \section Requirements
 *
 * This package can be used on SAMA5D2x Xplained board.
 *
 * \section Description
 *
 * This example writes the getting-started code into flash via SPI and enables
 * quad mode spi to read code and to execute from it.
 *
 * \section Usage
 *
 *  -# Build the program and download it inside the evaluation board. Please
 *     refer to the
 *     <a href="http://www.atmel.com/dyn/resources/prod_documents/6421B.pdf">
 *     SAM-BA User Guide</a>, the
 *     <a href="http://www.atmel.com/dyn/resources/prod_documents/doc6310.pdf">
 *     GNU-Based Software Development</a>
 *     application note or to the
 *     <a href="ftp://ftp.iar.se/WWWfiles/arm/Guides/EWARM_UserGuide.ENU.pdf">
 *     IAR EWARM User Guide</a>,
 *     depending on your chosen solution.
 *  -# On the computer, open and configure a terminal application (e.g.
 *     HyperTerminal on Microsoft Windows) with these settings:
 *        - 115200 bauds
 *        - 8 data bits
 *        - No parity
 *        - 1 stop bit
 *        - No flow control
 *  -# Start the application. The following traces shall appear on the terminal:
 *     \code
 *      -- QSPI XIP Example xxx --
 *      -- SAMxxxxx-xx
 *      -- Compiled: xxx xx xxxx xx:xx:xx --
 *      QSPI drivers initialized
 *    \endcode
 * \section References
 * - qspi_xip/main.c
 * - qspiflash.c
 */

/**
 * \file
 *
 * This file contains all the specific code for the qspi_xip example.
 */

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "board.h"
#include "chip.h"
#include "trace.h"
#include "compiler.h"

#include "cortex-a/mmu.h"
#include "cortex-a/cp15.h"

#include "peripherals/aic.h"
#include "peripherals/pio.h"
#include "peripherals/pmc.h"
#include "peripherals/qspi.h"
#include "peripherals/wdt.h"

#include "memories/qspiflash.h"
#include "misc/console.h"

#include "power/act8945a.h"

#include <stdbool.h>
#include <stdio.h>

#include "getting_started_hex.h"

/*----------------------------------------------------------------------------
 *        Local definitions
 *----------------------------------------------------------------------------*/

/** QSPI peripheral pins to configure to access the serial flash. */
#define QSPI_PINS        PINS_QSPI

/*----------------------------------------------------------------------------
 *        Local variables
 *----------------------------------------------------------------------------*/

#ifdef CONFIG_HAVE_PMIC_ACT8945A
struct _pin act8945a_pins[] = ACT8945A_PINS;
struct _twi_desc act8945a_twid = {
	.addr = ACT8945A_ADDR,
	.freq = ACT8945A_FREQ,
	.transfert_mode = TWID_MODE_POLLING
};
struct _act8945a act8945a = {
	.desc = {
		.pin_chglev = ACT8945A_PIN_CHGLEV,
		.pin_irq = ACT8945A_PIN_IRQ,
		.pin_lbo = ACT8945A_PIN_LBO
	}
};
#endif

/** Pins to configure for the application. */
static struct _pin pins_qspi[] = QSPIFLASH_PINS;

/** QSPI serial flash instance. */
static struct _qspiflash flash;

/*----------------------------------------------------------------------------
 *        Global functions
 *----------------------------------------------------------------------------*/

/**
 *  \brief QSPI_XIP Application entry point.
 *
 *  \return Unused (ANSI-C compatibility).
 */
int main(void)
{
	bool verify_failed = 0;
	uint32_t baudrate, idx;
	void (*xip_startup_fn)(void);
	void* qspi_mem_addr = get_qspi_mem_from_addr(QSPIFLASH_ADDR);
	uint32_t buffer[4];
	uint8_t *ptr;

	/* Disable watchdog */
	wdt_disable();

	/* Disable all PIO interrupts */
	pio_reset_all_it();

	/* Configure console */
	board_cfg_console();
	console_clear_screen();
	console_reset_cursor();

#ifndef VARIANT_DDRAM
	mmu_initialize();
	cp15_enable_mmu();
	cp15_enable_dcache();
	cp15_enable_icache();
#endif

	printf("-- QSPI XIP Example %s --\n\r", SOFTPACK_VERSION);
	printf("-- %s\n\r", BOARD_NAME);
	printf("-- Compiled: %s %s --\n\r", __DATE__, __TIME__);

#ifdef CONFIG_HAVE_PMIC_ACT8945A
	pio_configure(act8945a_pins, ARRAY_SIZE(act8945a_pins));
	if (act8945a_configure(&act8945a, &act8945a_twid)) {
		act8945a_set_regulator_voltage(&act8945a, 6, 2500);
		act8945a_enable_regulator(&act8945a, 6, true);
	} else {
		printf("-E- Error initializing ACT8945A PMIC\n\r");
	}
#endif

	/* Initialize the QSPI and serial flash */
	pio_configure(pins_qspi, ARRAY_SIZE(pins_qspi));

	trace_debug("Initializing QSPI drivers...\n\r");
	qspi_initialize(QSPIFLASH_ADDR);
	trace_debug("QSPI drivers initialized.\n\r");

	baudrate = qspi_set_baudrate(QSPIFLASH_ADDR, QSPIFLASH_BAUDRATE);
	trace_debug("QSPI baudrate set to %uHz\r\n", (unsigned)baudrate);

	trace_debug("Configuring QSPI Flash...\n\r");
	if (!qspiflash_configure(&flash, QSPIFLASH_ADDR)) {
		trace_fatal("Configure QSPI Flash failed!\n\r");
	}
	trace_debug("QSPI Flash configured.\n\r");

	/* get the code at the beginning of QSPI, run the code directly if it's valid */
	if (!qspiflash_read(&flash, 0, buffer, sizeof(buffer))) {
		trace_fatal("Read the code from QSPI Flash failed!\n\r");
	}
	printf("-I- Data at the beginning of QSPI: %08x %08x %08x %08x\n\r",
		(unsigned int)buffer[0], (unsigned int)buffer[1],
		(unsigned int)buffer[2], (unsigned int)buffer[3]);

	/* check whether or not there's a valid application in QSPI */
	if ((((buffer[0] & 0xFF000000) == 0xE5000000) ||
				((buffer[0] & 0xFF000000) == 0xEA000000)) &&
			((buffer[0] & 0xFF000000) == (buffer[1] & 0xFF000000)) &&
			((buffer[0] & 0xFF000000) == (buffer[2] & 0xFF000000)) &&
			((buffer[0] & 0xFF000000) == (buffer[3] & 0xFF000000)) ) {
		printf("-I- Valid application already in QSPI, will run it using QSPI XIP\n\r");
		printf("-I- Starting continuous read mode to enter in XIP mode\n\r");
		if (!qspiflash_read(&flash, 0, NULL, 0)) {
			trace_fatal("Read the code from QSPI Flash failed!\n\r");
		}
		xip_startup_fn = (void(*)(void))qspi_mem_addr;
		xip_startup_fn();
	} else {
		printf("-I- No valid application found in QSPI, will one program first\n\r");
	}

	printf("Erasing 16x4KB at beginning of memory...\n\r");
	for (idx = 0; idx < 16; idx++) {
		if (!qspiflash_erase_block(&flash, idx * 4096, 4096)) {
			trace_fatal("QSPI Flash block erase failed!\n\r");
		}
	}
	printf("Erase done.\n\r");

	/* Flash the code to QSPI flash */
	printf("Writing to QSPI...\n\r");
	if (!qspiflash_write(&flash, 0, xip_program, sizeof(xip_program))) {
		trace_fatal("QSPI Flash writing failed!\n\r");
	}
	printf("Example code written to memory (0x%x bytes).\n\r", sizeof(xip_program));
	printf("Verifying...\n\r");

	/* Start continuous read mode to enter in XIP mode*/
	if (!qspiflash_read(&flash, 0, NULL, 0)) {
		trace_fatal("QSPI Flash read failed!\n\r");
	}

	ptr = (uint8_t*)qspi_mem_addr;
	verify_failed = false;
	for (idx = 0; idx < sizeof(xip_program); idx++, ptr++) {
		if (*ptr != xip_program[idx]) {
			verify_failed = true;
			printf("-E- Data does not match at 0x%x (0x%02x != 0x%02x)\n\r",
					(unsigned)ptr, *ptr, xip_program[idx]);
			break;
		}
	}

	if (!verify_failed) {
		printf("Everything is OK \n\r");
		printf("\n\r Running code from QSPI flash \n\r");
		printf("========================================================= \n\r");

		xip_startup_fn = (void(*)(void))qspi_mem_addr;
		xip_startup_fn();
	}

	while (1);
}
