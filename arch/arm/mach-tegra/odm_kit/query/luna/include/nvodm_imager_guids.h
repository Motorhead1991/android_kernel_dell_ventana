/*
 * Copyright (c) 2007 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */
#ifndef NVODM_IMAGER_GUIDS_H
#define NVODM_IMAGER_GUIDS_H

#include "nvodm_query_discovery.h"


#define OV5630_GUID NV_ODM_GUID('s','_','O','V','5','6','3','0')


#define MI5130_GUID NV_ODM_GUID('s','_','M','I','5','1','3','0')
#define SEMCOVGA_GUID NV_ODM_GUID('s','_','S','M','P','Y','U','V')



#define DW9710_GUID NV_ODM_GUID('f','_','D','W','9','7','1','0')


#define AD5820_GUID NV_ODM_GUID('f','_','A','D','5','8','2','0')


#define LTC3216_GUID NV_ODM_GUID('l','_','L','T','3','2','1','6')


#define COMMONIMAGER_GUID NV_ODM_GUID('s', '_', 'c', 'o', 'm', 'm', 'o', 'n')





#define NVODM_CAMERA_DEVICE_IS_DEFAULT   (1)




#define NVODM_CAMERA_DATA_PIN_SHIFT      (1)
#define NVODM_CAMERA_DATA_PIN_MASK       0x0F
#define NVODM_CAMERA_PARALLEL_VD0_TO_VD9 (1 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_PARALLEL_VD0_TO_VD7 (2 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D1A      (4 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D2A      (5 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D1A_D2A  (6 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D1B      (7 << NVODM_CAMERA_DATA_PIN_SHIFT)








#define NVODM_IMAGER_GPIO_PIN_SHIFT    (24)
#define NVODM_IMAGER_UNUSED            (0x0)
#define NVODM_IMAGER_RESET             (0x1 << NVODM_IMAGER_GPIO_PIN_SHIFT)
#define NVODM_IMAGER_RESET_AL          (0x2 << NVODM_IMAGER_GPIO_PIN_SHIFT)
#define NVODM_IMAGER_POWERDOWN         (0x3 << NVODM_IMAGER_GPIO_PIN_SHIFT)
#define NVODM_IMAGER_POWERDOWN_AL      (0x4 << NVODM_IMAGER_GPIO_PIN_SHIFT)

#define NVODM_IMAGER_MCLK              (0x8 << NVODM_IMAGER_GPIO_PIN_SHIFT)

#define NVODM_IMAGER_PWM               (0x9 << NVODM_IMAGER_GPIO_PIN_SHIFT)


#define NVODM_IMAGER_FLASH0           (0x5 << NVODM_IMAGER_GPIO_PIN_SHIFT)
#define NVODM_IMAGER_FLASH1           (0x6 << NVODM_IMAGER_GPIO_PIN_SHIFT)
#define NVODM_IMAGER_FLASH2           (0x7 << NVODM_IMAGER_GPIO_PIN_SHIFT)

#define NVODM_IMAGER_SHUTTER          (0xA << NVODM_IMAGER_GPIO_PIN_SHIFT)


#define NVODM_IMAGER_MASK              (0xF << NVODM_IMAGER_GPIO_PIN_SHIFT)
#define NVODM_IMAGER_CLEAR(_s)         ((_s) & ~(NVODM_IMAGER_MASK))
#define NVODM_IMAGER_IS_SET(_s)        (((_s) & (NVODM_IMAGER_MASK)) != 0)
#define NVODM_IMAGER_FIELD(_s)         ((_s) >> NVODM_IMAGER_GPIO_PIN_SHIFT)

#endif
