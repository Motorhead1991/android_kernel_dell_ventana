/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "nvodm_query_gpio.h"
#include "nvodm_services.h"
#include "nvrm_drf.h"
#include "nvodm_query_discovery.h"

#include "linux/input.h"

#define NVODM_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define NVODM_PORT(x) ((x) - 'a')


static const NvOdmGpioPinInfo s_vi[] = {
    {NVODM_PORT('t'), 3, NvOdmGpioPinActiveState_High}, 
};

static const NvOdmGpioPinInfo s_display[] = {


    
    { NVODM_PORT('m'), 3, NvOdmGpioPinActiveState_Low },
    { NVODM_PORT('b'), 2, NvOdmGpioPinActiveState_Low },
    { NVODM_PORT('n'), 4, NvOdmGpioPinActiveState_Low },
    { NVODM_PORT('j'), 3, NvOdmGpioPinActiveState_Low },
    { NVODM_PORT('j'), 4, NvOdmGpioPinActiveState_Low },
    
    {NVODM_GPIO_INVALID_PORT, NVODM_GPIO_INVALID_PIN,
        NvOdmGpioPinActiveState_Low},

    
    {NVODM_GPIO_INVALID_PORT, NVODM_GPIO_INVALID_PIN,
        NvOdmGpioPinActiveState_Low},
    {NVODM_GPIO_INVALID_PORT, NVODM_GPIO_INVALID_PIN,
        NvOdmGpioPinActiveState_Low},
    {NVODM_GPIO_INVALID_PORT, NVODM_GPIO_INVALID_PIN,
        NvOdmGpioPinActiveState_Low},
    {NVODM_GPIO_INVALID_PORT, NVODM_GPIO_INVALID_PIN,
        NvOdmGpioPinActiveState_Low},

    
    { NVODM_PORT('v'), 7, NvOdmGpioPinActiveState_Low },

    
    { NVODM_PORT('n'), 6, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('n'), 4, NvOdmGpioPinActiveState_Low },    
    { NVODM_PORT('b'), 3, NvOdmGpioPinActiveState_Low },    
    { NVODM_PORT('b'), 2, NvOdmGpioPinActiveState_Low },    
    { NVODM_PORT('e'), 0, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 1, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 2, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 3, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 4, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 5, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 6, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('e'), 7, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 0, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 1, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 2, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 3, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 4, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 5, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 6, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('f'), 7, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('m'), 3, NvOdmGpioPinActiveState_High },   

    
    { NVODM_PORT('v'), 7, NvOdmGpioPinActiveState_Low },

    
    { NVODM_PORT('b'), 2, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('w'), 0, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('b'), 5, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('c'), 6, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('b'), 4, NvOdmGpioPinActiveState_High },   

    
    { NVODM_PORT('s'), 1, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('z'), 3, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('r'), 7, NvOdmGpioPinActiveState_High },   

    
    { NVODM_PORT('s'), 1, NvOdmGpioPinActiveState_High },   
    { NVODM_PORT('r'), 7, NvOdmGpioPinActiveState_High },   
};

static const NvOdmGpioPinInfo s_hdmi[] =
{
    
    { NVODM_PORT('n'), 7, NvOdmGpioPinActiveState_High },    
};

static const NvOdmGpioPinInfo s_sdio2[] = {
    {NVODM_PORT('i'), 5, NvOdmGpioPinActiveState_Low},    
    
    {NVODM_PORT('h'), 2, NvOdmGpioPinActiveState_High},    
};

static const NvOdmGpioPinInfo s_NandFlash[] = {
    {NVODM_PORT('c'), 7, NvOdmGpioPinActiveState_High} 
};

static const NvOdmGpioPinInfo s_spi_ethernet[] = {
    {NVODM_PORT('s'), 5, NvOdmGpioPinActiveState_Low}   
};


static const NvOdmGpioPinInfo s_Power[] = {
    
    {NVODM_PORT('v'), 2, NvOdmGpioPinActiveState_Low}
};


static const NvOdmGpioPinKeyInfo s_GpioPinKeyInfo[] = {
    {KEY_END, 10, NV_TRUE},
    {KEY_VOLUMEDOWN, 10, NV_TRUE},
    {KEY_VOLUMEUP, 10, NV_TRUE},
};



static const NvOdmGpioPinInfo s_GpioKeyBoard[] = {
    {NVODM_PORT('v'), 2, NvOdmGpioPinActiveState_Low, (void *)&s_GpioPinKeyInfo[0]},
    {NVODM_PORT('q'), 0, NvOdmGpioPinActiveState_Low, (void *)&s_GpioPinKeyInfo[1]},
    {NVODM_PORT('q'), 1, NvOdmGpioPinActiveState_Low, (void *)&s_GpioPinKeyInfo[2]},
};

const NvOdmGpioPinInfo *NvOdmQueryGpioPinMap(NvOdmGpioPinGroup Group,
    NvU32 Instance, NvU32 *pCount)
{

    switch (Group)
    {
        case NvOdmGpioPinGroup_Display:
            *pCount = NVODM_ARRAY_SIZE(s_display);
            return s_display;

        case NvOdmGpioPinGroup_Hdmi:
            *pCount = NVODM_ARRAY_SIZE(s_hdmi);
            return s_hdmi;


        case NvOdmGpioPinGroup_Sdio:
            if (Instance == 2)
            {
                *pCount = NVODM_ARRAY_SIZE(s_sdio2);
                return s_sdio2;
            }
            else
            {
                *pCount = 0;
                return NULL;
            }

        case NvOdmGpioPinGroup_NandFlash:
            *pCount = NVODM_ARRAY_SIZE(s_NandFlash);
            return s_NandFlash;


        case NvOdmGpioPinGroup_SpiEthernet:
            if (NvOdmQueryDownloadTransport() ==
                NvOdmDownloadTransport_SpiEthernet)
            {
                *pCount = NVODM_ARRAY_SIZE(s_spi_ethernet);
                return s_spi_ethernet;
            }
            else
            {
                *pCount = 0;
                return NULL;
            }
        
        case NvOdmGpioPinGroup_Vi:
            *pCount = NVODM_ARRAY_SIZE(s_vi);
            return s_vi;

        case NvOdmGpioPinGroup_Power:
            *pCount = NVODM_ARRAY_SIZE(s_Power);
            return s_Power;


        case NvOdmGpioPinGroup_keypadMisc:
            *pCount = NVODM_ARRAY_SIZE(s_GpioKeyBoard);
            return s_GpioKeyBoard;

        default:
            *pCount = 0;
            return NULL;
    }
}
