/*
 * Bluetooth+WiFi Murata LBEE19QMBC rfkill power control via GPIO
 *
 * Copyright (C) 2010 NVIDIA Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __BCOM4329_RFKILL_H
#define __BCOM4329_RFKILL_H

struct bcom4329_platform_data {
    int gpio_reset;
    int gpio_enable;
    int gpio_wakepin;
};

#endif
