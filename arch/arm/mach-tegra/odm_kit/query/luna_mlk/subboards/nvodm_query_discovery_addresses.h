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



#include "../../../adaptations/pmu/tps6586x/nvodm_pmu_tps6586x_supply_info_table.h"
#include "../../../adaptations/tmon/adt7461/nvodm_tmon_adt7461_channel.h"
#include "nvodm_tmon.h"
#include "../nvodm_query_kbc_gpio_def.h"
#include "../include/nvodm_query_discovery_imager.h"


static const NvOdmIoAddress s_RtcAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO2, 0 }  
};


static const NvOdmIoAddress s_CoreAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_DCD0, 0 }  
};


static const NvOdmIoAddress s_ffaCpuAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_DCD1, 0 }  
};


static const NvOdmIoAddress s_PllAAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_PllMAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_PllPAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_PllCAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_PllEAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_Invalid, 0 } 
};


static const NvOdmIoAddress s_PllUAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_ffaPllU1Addresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_PllSAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_Invalid, 0 } 
};


static const NvOdmIoAddress s_PllHdmiAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO8, 0 } 
};


static const NvOdmIoAddress s_VddOscAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO4, 0 } 
};


static const NvOdmIoAddress s_PllXAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO1, 0 } 
};


static const NvOdmIoAddress s_PllUsbAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO3, 0 } 
};


static const NvOdmIoAddress s_VddSysAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO4, 0 } 
};


static const NvOdmIoAddress s_VddUsbAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO3, 0 } 
};


static const NvOdmIoAddress s_VddHdmiAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO7, 0 } 
};


static const NvOdmIoAddress s_VddMipiAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_Invalid , 0 } 
};


static const NvOdmIoAddress s_VddLcdAddresses[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS72012PmuSupply_LDO, 0 } 
};


static const NvOdmIoAddress s_VddAudAddresses[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 } 
};


static const NvOdmIoAddress s_VddDdrAddresses[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 }  
};


static const NvOdmIoAddress s_VddDdrRxAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 }  
};


static const NvOdmIoAddress s_VddNandAddresses[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 }  
};


static const NvOdmIoAddress s_VddUartAddresses[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 } 
};


static const NvOdmIoAddress s_VddSdioAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO6, 0 } 
};


static const NvOdmIoAddress s_VddVdacAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_Invalid, 0 } 
};


static const NvOdmIoAddress s_VddViAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 } 
};


static const NvOdmIoAddress s_VddBbAddresses[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO4, 0 } 
};


static const NvOdmIoAddress s_VddSocAddresses[]=
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_SoC, 0 } 
};


static const NvOdmIoAddress s_VddPexClkAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_Invalid, 0 }, 
};


static const NvOdmIoAddress s_Pmu0Addresses[] =
{
    { NvOdmIoModule_I2c_Pmu, 0x00, 0x68, 0 },
    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x4, 1 },		
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x7, 2 },		
    
};

#if 0
static const NvOdmIoAddress s_Vddio_Vid_En[] = {
    { NvOdmIoModule_Gpio, 't'-'a', 2, 0 },
};

static const NvOdmIoAddress s_Vddio_Sd_En[] = {
    { NvOdmIoModule_Gpio, 't'-'a', 3, 0 },
};

static const NvOdmIoAddress s_Vddio_Sdmmc_En[] = {
    { NvOdmIoModule_Gpio, 'i'-'a', 6, 0 },
};

static const NvOdmIoAddress s_Vddio_Bl_En[] = {
    { NvOdmIoModule_Gpio, 'w'-'a', 0, 0 },
};

static const NvOdmIoAddress s_Vddio_Pnl_En[] = {
    { NvOdmIoModule_Gpio, 'c'-'a', 6, 0 },
};
#endif


static const NvOdmIoAddress s_SpiEthernetAddresses[] =
{
    { NvOdmIoModule_Spi, 0, 0, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 5, 0 },  
};


static const NvOdmIoAddress s_AuoDisplayAddresses[] =
{
    { NvOdmIoModule_Display, 0x00, 0x00, 0 },
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO0, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'z'-'a', 3, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 1, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 7, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 4, 0 },
};


static const NvOdmIoAddress s_SamsungDisplayAddresses[] =
{
    { NvOdmIoModule_Display, 0x00, 0x00, 0 },
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO0, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 1, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 7, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 4, 0 },
};


static const NvOdmIoAddress s_HdmiAddresses[] =
{
    { NvOdmIoModule_Hdmi, 0, 0, 0 },

    
    
    { NvOdmIoModule_I2c, 0x01, 0xA0, 0 },

    
    { NvOdmIoModule_I2c, 0x01, 0x74, 0 },

    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO7, 0 },
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO8, 0 },

    
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS72012PmuSupply_LDO, 0 },
    
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS62290PmuSupply_BUCK, 0 },
};


static const NvOdmIoAddress s_HdmiHotplug[] =
{
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO4, 0 },
    
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS62290PmuSupply_BUCK, 0 },
};


static const NvOdmIoAddress s_SdioAddresses[] =
{
    { NvOdmIoModule_Sdio, 0x0,  0x0, 0 },                      
    { NvOdmIoModule_Sdio, 0x2,  0x0, 0 },                      
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO6, 0 } 
};


static const NvOdmIoAddress s_SdioHsmmcAddresses[] =
{
    { NvOdmIoModule_Sdio, 0x0, 0x0, 0 },
    { NvOdmIoModule_Sdio, 0x2, 0x0, 0 },
    { NvOdmIoModule_Sdio, 0x3, 0x0, 0 },
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 },
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO6, 0 },
};


static const NvOdmIoAddress s_Tmon0Addresses[] =
{
    
    
    { NvOdmIoModule_I2c, 0x02, 0x98, 0 },                  
    
    
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO3, 0 },    
    { NvOdmIoModule_Gpio, (NvU32)'n'-'a', 6, 0 },              

    
    { NvOdmIoModule_Tsense, NvOdmTmonZoneID_Core, ADT7461ChannelID_Remote, 0 },   
    { NvOdmIoModule_Tsense, NvOdmTmonZoneID_Ambient, ADT7461ChannelID_Local, 0 }, 
};

#if 1



static const NvOdmIoAddress s_WlanAddresses[] =
{

    { NvOdmIoModule_Gpio, (NvU32)'x' - 'a', 0x1, 0 },                      

};
#endif


static const NvOdmIoAddress s_AudioCodecAddresses[] =
{
    { NvOdmIoModule_ExternalClock, 0, 0, 0 },       
    { NvOdmIoModule_I2c, 0x00, 0x34, 0 },           
                                                 
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x0, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x4, 0 }, 

};


static const NvOdmIoAddress s_ImagerOv9665Addresses[] =
{
    { NvOdmIoModule_I2c, 0x02, 0x60, 0 },
    { NvOdmIoModule_Gpio, (NvU32)27, 5 | NVODM_IMAGER_RESET_AL, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'d'-'a', 2 | NVODM_IMAGER_POWERDOWN, 0 },  
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 },  
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO3, 0 },  
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 1 },  
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO0, 1 },  
    { NvOdmIoModule_VideoInput, 0x00, YUVSENSOR_PINS, 0 },
    { NvOdmIoModule_ExternalClock, 0x02, 0x00, 0 },
};


static const NvOdmIoAddress s_I2cKeyboardAddresses[] =
{
    { NvOdmIoModule_I2c, 0x02, 0xD0, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'b'-'a', 1 | 0x00010000, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'t'-'a', 5 | 0x00000000, 0 },
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO3, 0 },
};


static const NvOdmIoAddress s_GpsBcm4751Addresses[] =
{
    { NvOdmIoModule_Uart, 0x03, 0x00, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 7, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 3, 0 },
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 0 },
};



static const NvOdmIoAddress s_GSensorAddresses[] =
{
    { NvOdmIoModule_I2c,  0x2, 0x38, 0 },				
    { NvOdmIoModule_Gpio, (NvU32)'a'-'a', 0x0, 0 },		
};

static const NvOdmIoAddress s_GSensorEvt1BAddresses[] =
{
    { NvOdmIoModule_I2c,  0x2, 0x38, 0 },				
    { NvOdmIoModule_Gpio, 27 , 0x4, 0 },					
};

static const NvOdmIoAddress s_ECompassAddresses[] =
{
    { NvOdmIoModule_I2c,  0x2, 0x1C, 0 },				
    { NvOdmIoModule_Gpio, 27 , 0x4, 0 },			
    { NvOdmIoModule_Gpio, 27 , 0x1, 0 },			
};

static const NvOdmIoAddress s_ECompassEvt1BAddresses[] =
{
    { NvOdmIoModule_I2c,  0x2, 0x1C, 0 },				
    { NvOdmIoModule_Gpio, (NvU32) 't' - 'a', 0x3, 0 },			
    { NvOdmIoModule_Gpio, (NvU32) 't' - 'a', 0x2, 0 },			
};


static const NvOdmIoAddress s_GpsBcm4329Address[] =
{
    { NvOdmIoModule_Uart, 0x02, 0x00, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 2, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'x'-'a', 0, 0 },
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 3, 0 },
};



static const NvOdmIoAddress s_LunaBattery_Evt1_Addresses[] =
{
    { NvOdmIoModule_I2c,  0x00,          0x80, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'j'-'a', 0x0, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'v'-'a', 0x3, 0 },		
};

static const NvOdmIoAddress s_LunaBattery_Evt1b_Addresses[] =
{
    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x4, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x0, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x1, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x4, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 0x0, 0 },		

    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x5, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x7, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'v'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'w'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'w'-'a', 0x3, 0 },		

    
    { NvOdmIoModule_I2c,  0x00, 0x6C         , 0 },		
};

static const NvOdmIoAddress s_LunaBattery_Evt13_Addresses[] =
{
    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x4, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x0, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x1, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x4, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 0x0, 0 },		

    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x5, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x7, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'v'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'w'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'w'-'a', 0x3, 0 },		

    
    { NvOdmIoModule_I2c,  0x00, 0x6C         , 0 },		
};

static const NvOdmIoAddress s_LunaBattery_Evt2_Addresses[] =
{
    
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x0, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x0, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x1, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'p'-'a', 0x2, 0 },		
    #if 1 
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x4, 0 },		
    #else 
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x0, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'r'-'a', 0x0, 0 },		
    #endif
    { NvOdmIoModule_Gpio, (NvU32)'u'-'a', 0x0, 0 },		

    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x5, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x7, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'v'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'w'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'w'-'a', 0x3, 0 },		

    
    { NvOdmIoModule_I2c,  0x00, 0x6C         , 0 },		
};

static const NvOdmIoAddress s_LunaVibratorAddresses[] =
{
    { NvOdmIoModule_I2c,  0x00, 0x90         , 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x2, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x3, 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'k'-'a', 0x6, 0 },		
    { NvOdmIoModule_ExternalClock, 0x01, 0x00, 0 },   
};

static const NvOdmIoAddress s_LunaLsensorAddresses[] =
{
    { NvOdmIoModule_I2c,  0x02, 0x72         , 0 },		
    { NvOdmIoModule_Gpio, (NvU32)'k'-'a', 0x7, 0 },		
};

static const NvOdmIoAddress s_LunaLedAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_GREEN1, 0 }, 
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_RED1, 0 },   
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_RGB1_BLINK_CONTROL, 0 }, 
};



static const NvOdmIoAddress s_TouchPanelAddresses[] =
{
    { NvOdmIoModule_I2c, 0x02, 0x4A, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'i'-'a', 0x3, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'i'-'a', 0x6, 0 }, 
    { NvOdmIoModule_Vdd,  0x00, TPS6586xPmuSupply_LDO5, 0 }, 
	{ NvOdmIoModule_Vdd,  0x00, TPS6586xPmuSupply_LDO9, 0 }, 
	{ NvOdmIoModule_Vdd,  0x00, TPS6586xPmuSupply_LDO3, 0 } 
};

static const NvOdmIoAddress s_keypadAddresses[] =
{
    { NvOdmIoModule_Gpio, (NvU32)'v'-'a', 0x2, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x0, 0 }, 
	{ NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x1, 0 }, 
	{ NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x6, 0 }, 
	{ NvOdmIoModule_Gpio, (NvU32)'q'-'a', 0x7, 0 }, 
	{ NvOdmIoModule_Gpio, (NvU32)'s'-'a', 0x2, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'b'-'a', 0x2, 0 }, 
	{ NvOdmIoModule_Gpio, (NvU32)'z'-'a', 0x2, 0 }, 
	{ NvOdmIoModule_Vdd, 0x00, Ext_TPS72012PmuSupply_LDO }, 
};

static const NvOdmIoAddress s_CapkeyAddresses[] =
{
    { NvOdmIoModule_I2c, 0x02, 0x68, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'b'-'a', 0x1, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'t'-'a', 0x5, 0 }, 
    { NvOdmIoModule_Vdd,  0x00, TPS6586xPmuSupply_LDO3, 0 }, 
};



static const NvOdmIoAddress s_DataCardAddresses[] =
{
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS72012PmuSupply_LDO, 0 }, 
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS74201PmuSupply_LDO, 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'h'-'a', 0x1           , 0 }, 
    { NvOdmIoModule_Gpio, (NvU32)'h'-'a', 0x3           , 0 }, 
};
static const NvOdmIoAddress s_UsimPlugAddresses[] =
{
    { NvOdmIoModule_Gpio, (NvU32)'v'-'a', 0x0 },            
};


static const NvOdmIoAddress s_ImagerOv5642Addresses[] =
{
    { NvOdmIoModule_I2c, 0x02, 0x78, 0 },                                       
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x2 | NVODM_IMAGER_RESET_AL,  0 },    
    { NvOdmIoModule_Gpio, (NvU32)'o'-'a', 0x1 | NVODM_IMAGER_POWERDOWN, 0 },    
    { NvOdmIoModule_Vdd, 0x00, Ext_TPS72012PmuSupply_LDO, 1 },                  
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO5, 1 },                     
    { NvOdmIoModule_Vdd, 0x00, TPS6586xPmuSupply_LDO0, 1 },                     
    { NvOdmIoModule_Vdd, 0x00, Ext_TPSGPIO4PmuSupply_LDO, 1 },                  
    { NvOdmIoModule_VideoInput, 0x00, CUST_IMAGER_PINS, 0 },
    { NvOdmIoModule_ExternalClock, 0x02, 0x00, 0 },
};


