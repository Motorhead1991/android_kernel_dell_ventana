
#include <linux/usb.h>
#include <mach/usb-hcd.h>
#include <asm/io.h>
#include "../../../drivers/usb/core/hcd.h"

#define UTMIP_TX_CFG0		0x820
#define UTMIP_FS_PREABMLE_J		(1 << 19)
#define UTMIP_HS_DISCON_DISABLE	(1 << 8)
 
#define UTMIP_MISC_CFG0	0x824
#define UTMIP_SUSPEND_EXIT_ON_EDGE	(1 << 22)


static void utmi_phy_preresume(struct usb_hcd *hcd)
{
	unsigned long val;
	void __iomem *base = hcd->regs;

	val = readl(base + UTMIP_TX_CFG0);
	val |= UTMIP_HS_DISCON_DISABLE;
	writel(val, base + UTMIP_TX_CFG0);
}

static void utmi_phy_postresume(struct usb_hcd *hcd)
{
	unsigned long val;
	void __iomem *base = hcd->regs;

	val = readl(base + UTMIP_TX_CFG0);
	val &= ~UTMIP_HS_DISCON_DISABLE;
	writel(val, base + UTMIP_TX_CFG0);
}

int tegra_usb_phy_preresume(struct usb_hcd *hcd)
{
	struct tegra_hcd_platform_data *pdata;

	pdata = hcd->self.controller->platform_data;
	if (pdata->instance == 2)
		utmi_phy_preresume(hcd);
	return 0;
}

int tegra_usb_phy_postresume(struct usb_hcd *hcd)
{
	struct tegra_hcd_platform_data *pdata;

	pdata = hcd->self.controller->platform_data;
	if (pdata->instance == 2)
		utmi_phy_postresume(hcd);
	return 0;
}

