/*
 * (C) Copyright alphascale
 *
 * (C) Copyright 2014 Du Huanpeng <u74147@gmail.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/arch/hardware.h>
#include <asm/io.h>

#define WDT_SETMODE_RESET     ((u32)0x00000003)


/*
 *  Initialize the Watchdog.
 */
void wdt_init(void)
{
	writel(1<<26, HW_PRESETCTRL0+8); /* reset watchdog */
	writel(1<<26, HW_PRESETCTRL0+4); /* clear watchdog reset */
	writel(1<<26, HW_AHBCLKCTRL0+4);
	writel(1, HW_WDTCLKSEL);
	writel(0, HW_WDTCLKUEN);
	writel(1, HW_WDTCLKUEN);
	writel(480, HW_WDTCLKDIV);       /* clkdiv=480 1MHz */

}

/*
 *  Sets WDG mode.
 *  param  Mode: specifies the WDG work mode.
 *   This parameter can be one of the following values.
 *      WDG_SETMODE_IT: Watchdog interrupt mode.
 *      WDG_SETMODE_RESET: Watchdog reset mode.
 */
void wdt_setmode(u32 mode)
{
	writel(mode, HW_WATCHDOG_WDMOD);
}

/*
 *  Sets WDG reload value.
 *  param  reload: specifies the IWDG reload value.
 *   This parameter must be a number between 0 and 0x0FFF.
 */
void wdt_setreload(u32 reload)
{
	writel(reload, HW_WATCHDOG_WDTC);
}

/*
 *  reloads WDG counter with value defined in the WDTC register
 *  param  None
 */
void wdt_reloadcounter(void)
{
	writel(0xaa, HW_WATCHDOG_WDFEED);
	writel(0x55, HW_WATCHDOG_WDFEED);
}

/*
 * reset the cpu by setting up the watchdog timer and let him time out
 */
void reset_cpu(ulong ignored)
{
	wdt_init();
	wdt_setmode(WDT_SETMODE_RESET);
	wdt_setreload(50);
	wdt_reloadcounter();
}
