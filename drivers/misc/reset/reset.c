#include <linux/init.h>
#include <linux/module.h>
#include "nvodm_services.h"
#include "../../../../kernel/arch/arm/mach-tegra/odm_kit/adaptations/pmu/tps6586x/nvodm_pmu_tps6586x_supply_info_table.h"





static int reset_init(void)
{
    NvOdmServicesPmuHandle hPmu = NvOdmServicesPmuOpen();
    NvOdmServicesPmuSetVoltage(hPmu, TPS6586xPmuSupply_SoftRst, 1, NULL);
    return 0;
}

static void reset_exit(void)
{
    printk(KERN_INFO "Goodbye, reset chip\n");
}

module_init(reset_init);
module_exit(reset_exit);
