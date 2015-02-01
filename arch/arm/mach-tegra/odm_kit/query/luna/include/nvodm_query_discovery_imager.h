/*
 * Copyright (c) 2007 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef NVODM_QUERY_DISCOVERY_IMAGER_H
#define NVODM_QUERY_DISCOVERY_IMAGER_H


#include "nvodm_query_discovery.h"

#if defined(__cplusplus)
extern "C"
{
#endif


#define SENSOR_BAYER_GUID           NV_ODM_GUID('s', '_', 'S', 'M', 'P', 'B', 'A', 'Y')
#define SENSOR_YUV_GUID             NV_ODM_GUID('s', '_', 'S', 'M', 'P', 'Y', 'U', 'V')
#define FOCUSER_GUID                NV_ODM_GUID('f', '_', 'S', 'M', 'P', 'F', 'O', 'C')
#define FLASH_GUID                  NV_ODM_GUID('f', '_', 'S', 'M', 'P', 'F', 'L', 'A')


#define SENSOR_BAYER_OV5630_GUID    NV_ODM_GUID('s', '_', 'O', 'V', '5', '6', '3', '0')
#define SENSOR_BAYER_OV5650_GUID    NV_ODM_GUID('s', '_', 'O', 'V', '5', '6', '5', '0')
#define SENSOR_BYRST_OV5650_GUID    NV_ODM_GUID('s', 't', 'O', 'V', '5', '6', '5', '0')


#define FOCUSER_AD5820_GUID         NV_ODM_GUID('f', '_', 'A', 'D', '5', '8', '2', '0')


#define FLASH_LTC3216_GUID          NV_ODM_GUID('l', '_', 'L', 'T', '3', '2', '1', '6')


#define SENSOR_YUV_OV9665_GUID	    NV_ODM_GUID('s', '-', 'O', 'V', '9', '6', '6', '5')


#define SENSOR_YUV_OV5642_GUID	    NV_ODM_GUID('s', '-', 'O', 'V', '5', '6', '4', '2')





#define NVODM_CAMERA_DEVICE_IS_DEFAULT      (1)




#define NVODM_CAMERA_DATA_PIN_SHIFT         (1)
#define NVODM_CAMERA_DATA_PIN_MASK          (0x0F)
#define NVODM_CAMERA_PARALLEL_VD0_TO_VD9    (1 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_PARALLEL_VD0_TO_VD7    (2 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D1A         (4 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D2A         (5 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D1A_D2A     (6 << NVODM_CAMERA_DATA_PIN_SHIFT)
#define NVODM_CAMERA_SERIAL_CSI_D1B         (7 << NVODM_CAMERA_DATA_PIN_SHIFT)








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

#define YUVSENSOR_PINS  (NVODM_CAMERA_PARALLEL_VD0_TO_VD9 | NVODM_CAMERA_DEVICE_IS_DEFAULT)
#define CUST_IMAGER_PINS  (NVODM_CAMERA_SERIAL_CSI_D1A | NVODM_CAMERA_DEVICE_IS_DEFAULT)

#if defined(__cplusplus)
}
#endif

#endif 
