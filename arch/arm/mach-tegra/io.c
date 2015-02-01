/*
 * arch/arm/mach-tegra/io.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/page.h>
#include <asm/mach/map.h>

#include "board.h"

#include <asm/sizes.h>
#include <linux/bootmem.h>
#ifdef CONFIG_WLAN_ALLOC_STATIC_MEM    
        #define ADDR_4329_DHD_PORT_LEN          SZ_16K    
        #define ADDR_4329_BUS_RXBUF_LEN         SZ_16K    
        #define ADDR_4329_BUS_DATABUF_LEN       SZ_32K    
        void *addr_4329_dhd_prot,*addr_4329_bus_rxbuf,*addr_4329_bus_databuf;
	EXPORT_SYMBOL(addr_4329_dhd_prot);
	EXPORT_SYMBOL(addr_4329_bus_rxbuf);
	EXPORT_SYMBOL(addr_4329_bus_databuf);
#endif

static struct map_desc tegra_io_desc[] __initdata = {
	{
		.virtual = IO_PPSB_VIRT,
		.pfn = __phys_to_pfn(IO_PPSB_PHYS),
		.length = IO_PPSB_SIZE,
		.type = MT_DEVICE,
	},
	{
		.virtual = IO_APB_VIRT,
		.pfn = __phys_to_pfn(IO_APB_PHYS),
		.length = IO_APB_SIZE,
		.type = MT_DEVICE,
	},
	{
		.virtual = IO_CPU_VIRT,
		.pfn = __phys_to_pfn(IO_CPU_PHYS),
		.length = IO_CPU_SIZE,
		.type = MT_DEVICE,
	},
	{
		.virtual = IO_IRAM_VIRT,
		.pfn = __phys_to_pfn(IO_IRAM_PHYS),
		.length = IO_IRAM_SIZE,
		.type = MT_DEVICE,
	},
};

#ifdef CONFIG_ROUTE_PRINTK_TO_MAINLOG
extern void init_reset_key_logger(void);
#endif
void __init tegra_map_common_io(void)
{
	iotable_init(tegra_io_desc, ARRAY_SIZE(tegra_io_desc));
#ifdef CONFIG_ROUTE_PRINTK_TO_MAINLOG
        init_reset_key_logger();
#endif
#ifdef CONFIG_WLAN_ALLOC_STATIC_MEM	
	addr_4329_dhd_prot	= alloc_bootmem(ADDR_4329_DHD_PORT_LEN);	
	addr_4329_bus_rxbuf	= alloc_bootmem(ADDR_4329_BUS_RXBUF_LEN);	
	addr_4329_bus_databuf	= alloc_bootmem(ADDR_4329_BUS_DATABUF_LEN);	
	printk(KERN_CRIT "dhd_port=0x%p,bus_rxbuf=0x%p,bus_databuf=0x%p\n",	      		
	      addr_4329_dhd_prot,addr_4329_bus_rxbuf,addr_4329_bus_databuf);
#endif
}

/*
 * Intercept ioremap() requests for addresses in our fixed mapping regions.
 */
void __iomem *tegra_ioremap(unsigned long p, size_t size, unsigned int type)
{
	void __iomem *v = IO_ADDRESS(p);
	if (v == NULL)
		v = __arm_ioremap(p, size, type);
	return v;
}
EXPORT_SYMBOL(tegra_ioremap);

void tegra_iounmap(volatile void __iomem *addr)
{
	unsigned long virt = (unsigned long)addr;

	if (virt >= VMALLOC_START && virt < VMALLOC_END)
		__iounmap(addr);
}
EXPORT_SYMBOL(tegra_iounmap);
