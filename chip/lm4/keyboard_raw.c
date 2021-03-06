/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by keyboard scanner module for Chrome EC */

#include "common.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"

void keyboard_raw_init(void)
{
	/* Ensure top-level interrupt is disabled */
	keyboard_raw_enable_interrupt(0);

	/*
	 * Set column outputs as open-drain; we either pull them low or let
	 * them float high.
	 */
	LM4_GPIO_AFSEL(LM4_GPIO_P) = 0;  /* KSO[7:0] */
	LM4_GPIO_AFSEL(LM4_GPIO_Q) &= ~0x1f;  /* KSO[12:8] */
	LM4_GPIO_DEN(LM4_GPIO_P) = 0xff;
	LM4_GPIO_DEN(LM4_GPIO_Q) |= 0x1f;
	LM4_GPIO_DIR(LM4_GPIO_P) = 0xff;
	LM4_GPIO_DIR(LM4_GPIO_Q) |= 0x1f;
	LM4_GPIO_ODR(LM4_GPIO_P) = 0xff;
	LM4_GPIO_ODR(LM4_GPIO_Q) |= 0x1f;

	/* Set row inputs with pull-up */
	LM4_GPIO_AFSEL(KB_SCAN_ROW_GPIO) &= 0xff;
	LM4_GPIO_DEN(KB_SCAN_ROW_GPIO) |= 0xff;
	LM4_GPIO_DIR(KB_SCAN_ROW_GPIO) = 0;
	LM4_GPIO_PUR(KB_SCAN_ROW_GPIO) = 0xff;

	/* Edge-sensitive on both edges. */
	LM4_GPIO_IS(KB_SCAN_ROW_GPIO) = 0;
	LM4_GPIO_IBE(KB_SCAN_ROW_GPIO) = 0xff;

	/*
	 * Enable interrupts for the inputs.  The top-level interrupt is still
	 * masked off, so this won't trigger interrupts yet.
	 */
	LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0xff;
}

void keyboard_raw_task_start(void)
{
	task_enable_irq(KB_SCAN_ROW_IRQ);
}

test_mockable void keyboard_raw_drive_column(int col)
{
	if (col == KEYBOARD_COLUMN_NONE) {
		/* Tri-state all outputs */
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0xff;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0x1f;
	} else if (col == KEYBOARD_COLUMN_ALL) {
		/* Assert all outputs */
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0;
	} else {
		/* Assert a single output */
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0xff;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0x1f;
		if (col < 8)
			LM4_GPIO_DATA(LM4_GPIO_P, 1 << col) = 0;
		else
			LM4_GPIO_DATA(LM4_GPIO_Q, 1 << (col - 8)) = 0;
	}
}

test_mockable int keyboard_raw_read_rows(void)
{
	/* Bits are active-low, so invert returned levels */
	return LM4_GPIO_DATA(KB_SCAN_ROW_GPIO, 0xff) ^ 0xff;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		/*
		 * Clear pending interrupts before enabling them, because the
		 * raw interrupt status may have been tripped by keyboard
		 * scanning or, if a key is already pressed, by driving all the
		 * outputs.
		 *
		 * We won't lose keyboard events because the scanning task will
		 * explicitly check the raw row state before waiting for an
		 * interrupt.  If a key is pressed, the task won't wait.
		 */
		LM4_GPIO_ICR(KB_SCAN_ROW_GPIO) = 0xff;
		LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0xff;
	} else {
		LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0;
	}
}

/**
 * Interrupt handler for the entire GPIO bank of keyboard rows.
 */
static void keyboard_raw_interrupt(void)
{
	/* Clear all pending keyboard interrupts */
	LM4_GPIO_ICR(KB_SCAN_ROW_GPIO) = 0xff;

	/* Wake the scan task */
	task_wake(TASK_ID_KEYSCAN);
}
DECLARE_IRQ(KB_SCAN_ROW_IRQ, keyboard_raw_interrupt, 3);
