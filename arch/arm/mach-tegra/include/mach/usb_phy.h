#ifndef __MACH_USB_PHY_H
#define __MACH_USB_PHY_H

#include "../../../../../drivers/usb/core/hcd.h"

int tegra_usb_phy_preresume(struct usb_hcd *hcd);

int tegra_usb_phy_postresume(struct usb_hcd *hcd);

 
#endif 
