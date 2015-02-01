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



#include "nvodm_query.h"
#include "nvodm_query_gpio.h"
#include "nvodm_query_memc.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query_pins.h"
#include "nvodm_query_pins_ap20.h"
#include "tegra_devkit_custopt.h"
#include "nvodm_keylist_reserved.h"
#include "nvrm_drf.h"



#include <mach/luna_hwid.h>


#if !defined(NV_OAL)
#define NV_OAL (0)
#endif

#define NVODM_ENABLE_EMC_DVFS (1)




#define NVODM_PMU_INT_ENABLED (0)

static const NvU8
s_NvOdmQueryDeviceNamePrefixValue[] = {'T','e','g','r','a',0};

static const NvU8
s_NvOdmQueryManufacturerSetting[] = {'N','V','I','D','I','A',0};

static const NvU8
s_NvOdmQueryModelSetting[] = {'A','P','2','0',0};

static const NvU8
s_NvOdmQueryPlatformSetting[] = {'H','a','r','m','o','n','y',0};

static const NvU8
s_NvOdmQueryProjectNameSetting[] = {'O','D','M',' ','K','i','t',0};

static const NvOdmDownloadTransport
s_NvOdmQueryDownloadTransportSetting = NvOdmDownloadTransport_None;

static const NvOdmQuerySdioInterfaceProperty s_NvOdmQuerySdioInterfaceProperty[4] =
{
    { NV_FALSE, 10,  NV_TRUE, 0x8, NvOdmQuerySdioSlotUsage_wlan   },
    { NV_TRUE,   0, NV_FALSE, 0x5, NvOdmQuerySdioSlotUsage_unused },
    { NV_TRUE,   0, NV_FALSE, 0x5, NvOdmQuerySdioSlotUsage_Media  },
    { NV_TRUE,   0, NV_FALSE, 0x4, NvOdmQuerySdioSlotUsage_Boot   }
};

static const NvOdmQuerySpiDeviceInfo s_NvOdmQuerySpiDeviceInfoTable [] =
{
    {NvOdmQuerySpiSignalMode_0, NV_TRUE}    
};


static const NvOdmQuerySpiIdleSignalState s_NvOdmQuerySpiIdleSignalStateLevel[] =
{
    {NV_FALSE, NvOdmQuerySpiSignalMode_0, NV_FALSE}    
};

static const NvOdmQueryI2sInterfaceProperty s_NvOdmQueryI2sInterfacePropertySetting[] =
{
    {
        NvOdmQueryI2sMode_Master,               
        NvOdmQueryI2sLRLineControl_LeftOnLow,   
        NvOdmQueryI2sDataCommFormat_I2S,        
        NV_FALSE,                               
        0                                       
    },
    {
        NvOdmQueryI2sMode_Master,               
        NvOdmQueryI2sLRLineControl_LeftOnLow,   
        NvOdmQueryI2sDataCommFormat_I2S,        
        NV_FALSE,                               
        0                                       
    }
};


static const NvOdmQuerySpdifInterfaceProperty s_NvOdmQuerySpdifInterfacePropertySetting =
{
    NvOdmQuerySpdifDataCaptureControl_FromLeft
};

static const NvOdmQueryAc97InterfaceProperty s_NvOdmQueryAc97InterfacePropertySetting =
{
    NV_FALSE,
    NV_FALSE,
    NV_FALSE,
    NV_FALSE,
    NV_TRUE
};



static const NvOdmQueryI2sACodecInterfaceProp s_NvOdmQueryI2sACodecInterfacePropSetting[] =
{
    {
        NV_FALSE,                               
        0,                                      
        0x34,                                   
        NV_FALSE,                               
        NvOdmQueryI2sLRLineControl_LeftOnLow,   
        NvOdmQueryI2sDataCommFormat_I2S         
    }
};

static const NvOdmQueryDapPortConnection s_NvOdmQueryDapPortConnectionTable[] =
{
    
    { NvOdmDapConnectionIndex_Music_Path, 2,
    { {NvOdmDapPort_I2s1, NvOdmDapPort_Dap1, NV_TRUE},
      {NvOdmDapPort_Dap1, NvOdmDapPort_I2s1, NV_FALSE}
    }},

    
    { NvOdmDapConnectionIndex_BlueTooth_Codec, 3,
    { {NvOdmDapPort_Dap4, NvOdmDapPort_I2s1, NV_TRUE},
      {NvOdmDapPort_I2s1, NvOdmDapPort_Dap4, NV_FALSE},
      {NvOdmDapPort_I2s2, NvOdmDapPort_Dap1, NV_FALSE}
    }}
};




static const NvOdmQueryDapPortProperty s_NvOdmQueryDapPortInfoTable[] =
{
    {NvOdmDapPort_None, NvOdmDapPort_None , {0, 0, 0, 0} }, 
    
    {NvOdmDapPort_I2s1, NvOdmDapPort_HifiCodecType,
        {2, 16, 44100, NvOdmQueryI2sDataCommFormat_I2S}},   
    {NvOdmDapPort_None, NvOdmDapPort_None , {0, 0, 0, 0} }, 
    {NvOdmDapPort_None, NvOdmDapPort_None , {0, 0, 0, 0} }, 
    
    {NvOdmDapPort_I2s2, NvOdmDapPort_BlueTooth,
        {2, 16, 8000, NvOdmQueryI2sDataCommFormat_I2S}}     
};

static const NvOdmSdramControllerConfigAdv s_NvOdmHyS5c1GbEmcConfigTable[] =
{
    {
                  0x20,   
                166500,   
                  1000,   
                    46,   
        {
            0x0000000A,   
            0x00000021,   
            0x00000008,   
            0x00000003,   
            0x00000004,   
            0x00000004,   
            0x00000002,   
            0x0000000B,   
            0x00000003,   
            0x00000003,   
            0x00000002,   
            0x00000001,   
            0x00000003,   
            0x00000004,   
            0x00000003,   
            0x00000009,   
            0x0000000C,   
            0x000004DF,   
            0x00000000,   
            0x00000003,   
            0x00000003,   
            0x00000003,   
            0x00000003,   
            0x00000001,   
            0x00000009,   
            0x000000C8,   
            0x00000003,   
            0x00000009,   
            0x00000004,   
            0x0000000C,   
            0x00000002,   
            0x00000000,   
            0x00000000,   
            0x00000002,   
            0x00000000,   
            0x00000000,   
            0x00000083,   
            0xA04204AE,   
            0x007FD010,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
        }
    },
    {
                  0x20,   
                333000,   
                  1200,   
                    46,   
        {
            0x00000014,   
            0x00000042,   
            0x0000000F,   
            0x00000005,   
            0x00000004,   
            0x00000005,   
            0x00000003,   
            0x0000000A,   
            0x00000005,   
            0x00000005,   
            0x00000004,   
            0x00000001,   
            0x00000003,   
            0x00000004,   
            0x00000003,   
            0x00000009,   
            0x0000000C,   
            0x000009FF,   
            0x00000000,   
            0x00000003,   
            0x00000003,   
            0x00000005,   
            0x00000005,   
            0x00000001,   
            0x0000000E,   
            0x000000C8,   
            0x00000003,   
            0x00000011,   
            0x00000006,   
            0x0000000C,   
            0x00000002,   
            0x00000000,   
            0x00000000,   
            0x00000002,   
            0x00000000,   
            0x00000000,   
            0x00000083,   
            0xE034048B,   
            0x007FA010,   
            0x00004717,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
            0x00000000,   
        }
    }
};


static NvOdmWakeupPadInfo s_NvOdmWakeupPadInfo[] =
{
    {NV_FALSE,  0, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE,  1, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE,  2, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE,  3, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE,  4, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE,  5, NvOdmWakeupPadPolarity_AnyEdge}, 
    {NV_TRUE,  6, NvOdmWakeupPadPolarity_High},    
    {NV_TRUE,  7, NvOdmWakeupPadPolarity_Low}, 
    {NV_FALSE,  8, NvOdmWakeupPadPolarity_AnyEdge}, 
    {NV_FALSE,  9, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE, 10, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE, 11, NvOdmWakeupPadPolarity_Low},     
    {NV_TRUE,  12, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE, 13, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE, 14, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE,  15, NvOdmWakeupPadPolarity_AnyEdge}, 
    {NV_TRUE,  16, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE,  17, NvOdmWakeupPadPolarity_High},    
    {NV_TRUE,  18, NvOdmWakeupPadPolarity_Low},     
    {NV_TRUE,  19, NvOdmWakeupPadPolarity_AnyEdge}, 
    {NV_FALSE, 20, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE, 21, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE, 22, NvOdmWakeupPadPolarity_Low},     
    {NV_TRUE,  23, NvOdmWakeupPadPolarity_AnyEdge}, 
    {NV_TRUE,  24, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE, 25, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE, 26, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE, 27, NvOdmWakeupPadPolarity_High},    
    {NV_FALSE, 28, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE, 29, NvOdmWakeupPadPolarity_Low},     
    {NV_FALSE, 30, NvOdmWakeupPadPolarity_High}     
};


static NvU32
GetBctKeyValue(void)
{
    NvOdmServicesKeyListHandle hKeyList = NULL;
    NvU32 BctCustOpt = 0;

    hKeyList = NvOdmServicesKeyListOpen();
    if (hKeyList)
    {
        BctCustOpt =
            NvOdmServicesGetKeyValue(hKeyList,
                                     NvOdmKeyListId_ReservedBctCustomerOption);
        NvOdmServicesKeyListClose(hKeyList);
    }

    return BctCustOpt;
}

NvOdmDebugConsole
NvOdmQueryDebugConsole(void)
{
#if 1
    return NvOdmDebugConsole_UartA;
#else
    NvU32 CustOpt = GetBctKeyValue();
    switch (NV_DRF_VAL(TEGRA_DEVKIT, BCT_CUSTOPT, CONSOLE, CustOpt))
    {
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_CONSOLE_DEFAULT:
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_CONSOLE_DCC:
        return NvOdmDebugConsole_Dcc;
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_CONSOLE_NONE:
        return NvOdmDebugConsole_None;
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_CONSOLE_UART:
        return NvOdmDebugConsole_UartA +
            NV_DRF_VAL(TEGRA_DEVKIT, BCT_CUSTOPT, CONSOLE_OPTION, CustOpt);
    default:
        return NvOdmDebugConsole_None;
    }
#endif
}

NvOdmDownloadTransport
NvOdmQueryDownloadTransport(void)
{
    NvU32 CustOpt = GetBctKeyValue();

    switch (NV_DRF_VAL(TEGRA_DEVKIT, BCT_CUSTOPT, TRANSPORT, CustOpt))
    {
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_TRANSPORT_NONE:
        return NvOdmDownloadTransport_None;
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_TRANSPORT_USB:
        return NvOdmDownloadTransport_Usb;
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_TRANSPORT_ETHERNET:
        switch (NV_DRF_VAL(TEGRA_DEVKIT, BCT_CUSTOPT, ETHERNET_OPTION, CustOpt))
        {
        case TEGRA_DEVKIT_BCT_CUSTOPT_0_ETHERNET_OPTION_SPI:
        case TEGRA_DEVKIT_BCT_CUSTOPT_0_ETHERNET_OPTION_DEFAULT:
        default:
            return NvOdmDownloadTransport_SpiEthernet;
        }
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_TRANSPORT_UART:
        switch (NV_DRF_VAL(TEGRA_DEVKIT, BCT_CUSTOPT, UART_OPTION, CustOpt))
        {
        case TEGRA_DEVKIT_BCT_CUSTOPT_0_UART_OPTION_B:
            return NvOdmDownloadTransport_UartB;
        case TEGRA_DEVKIT_BCT_CUSTOPT_0_UART_OPTION_C:
            return NvOdmDownloadTransport_UartC;
        case TEGRA_DEVKIT_BCT_CUSTOPT_0_UART_OPTION_DEFAULT:
        case TEGRA_DEVKIT_BCT_CUSTOPT_0_UART_OPTION_A:
        default:
            return NvOdmDownloadTransport_UartA;
        }
    case TEGRA_DEVKIT_BCT_CUSTOPT_0_TRANSPORT_DEFAULT:
    default:
        return s_NvOdmQueryDownloadTransportSetting;
    }
}

const NvU8*
NvOdmQueryDeviceNamePrefix(void)
{
    return s_NvOdmQueryDeviceNamePrefixValue;
}

const NvOdmQuerySpiDeviceInfo *
NvOdmQuerySpiGetDeviceInfo(
    NvOdmIoModule OdmIoModule,
    NvU32 ControllerId,
    NvU32 ChipSelect)
{
    if (OdmIoModule == NvOdmIoModule_Spi)
    {
        switch (ControllerId)
        {
            case 0:
                if (ChipSelect == 0)
                    return &s_NvOdmQuerySpiDeviceInfoTable[0];
                break;

            default:
                break;
        }
        return NULL;
    }
    return NULL;
}

const NvOdmQuerySpiIdleSignalState *
NvOdmQuerySpiGetIdleSignalState(
    NvOdmIoModule OdmIoModule,
    NvU32 ControllerId)
{
    if (OdmIoModule == NvOdmIoModule_Spi)
    {
        if (ControllerId == 0)
            return &s_NvOdmQuerySpiIdleSignalStateLevel[0];
    }
    return NULL;
}

const NvOdmQueryI2sInterfaceProperty *
NvOdmQueryI2sGetInterfaceProperty(
    NvU32 I2sInstanceId)
{
    if ((I2sInstanceId == 0) || (I2sInstanceId == 1))
        return &s_NvOdmQueryI2sInterfacePropertySetting[I2sInstanceId];

    return NULL;
}

const NvOdmQueryDapPortProperty *
NvOdmQueryDapPortGetProperty(
    NvU32 DapPortId)
{
    if (DapPortId > 0 && DapPortId < NV_ARRAY_SIZE(s_NvOdmQueryDapPortInfoTable) )
        return &s_NvOdmQueryDapPortInfoTable[DapPortId];

    return NULL;
}

const NvOdmQueryDapPortConnection*
NvOdmQueryDapPortGetConnectionTable(
    NvU32 ConnectionIndex)
{
    NvU32 TableIndex   = 0;
    for( TableIndex = 0;
         TableIndex < NV_ARRAY_SIZE(s_NvOdmQueryDapPortConnectionTable);
         TableIndex++)
    {
        if (s_NvOdmQueryDapPortConnectionTable[TableIndex].UseIndex
                                                    == ConnectionIndex)
            return &s_NvOdmQueryDapPortConnectionTable[TableIndex];
    }
    return NULL;
}

const NvOdmQuerySpdifInterfaceProperty *
NvOdmQuerySpdifGetInterfaceProperty(
    NvU32 SpdifInstanceId)
{
    if (SpdifInstanceId == 0)
        return &s_NvOdmQuerySpdifInterfacePropertySetting;

    return NULL;
}

const NvOdmQueryAc97InterfaceProperty *
NvOdmQueryAc97GetInterfaceProperty(
    NvU32 Ac97InstanceId)
{
    if (Ac97InstanceId == 0)
        return &s_NvOdmQueryAc97InterfacePropertySetting;

    return NULL;
}

const NvOdmQueryI2sACodecInterfaceProp *
NvOdmQueryGetI2sACodecInterfaceProperty(
    NvU32 AudioCodecId)
{
    NvU32 NumInstance = sizeof(s_NvOdmQueryI2sACodecInterfacePropSetting)/
                            sizeof(s_NvOdmQueryI2sACodecInterfacePropSetting[0]);
    if (AudioCodecId < NumInstance)
        return &s_NvOdmQueryI2sACodecInterfacePropSetting[AudioCodecId];

    return NULL;
}


NvBool NvOdmQueryAsynchMemConfig(
    NvU32 ChipSelect,
    NvOdmAsynchMemConfig *pMemConfig)
{
    return NV_FALSE;
}

const void*
NvOdmQuerySdramControllerConfigGet(NvU32 *pEntries, NvU32 *pRevision)
{
#if NVODM_ENABLE_EMC_DVFS





            if (pRevision)
                *pRevision = s_NvOdmHyS5c1GbEmcConfigTable[0].Revision;
            if (pEntries)
                *pEntries = NV_ARRAY_SIZE(s_NvOdmHyS5c1GbEmcConfigTable);
            return (const void*)s_NvOdmHyS5c1GbEmcConfigTable;


#endif
    if (pEntries)
        *pEntries = 0;
    return NULL;
}

NvOdmQueryOscillator NvOdmQueryGetOscillatorSource(void)
{
    return NvOdmQueryOscillator_Xtal;
}

NvU32 NvOdmQueryGetOscillatorDriveStrength(void)
{
    
    if (system_rev < EVT2_3 || system_rev == HWID_UNKNOWN)
        return 0x04;
    else
        return 0x0A;
}

const NvOdmWakeupPadInfo *NvOdmQueryGetWakeupPadTable(NvU32 *pSize)
{
    if (pSize)
        *pSize = NV_ARRAY_SIZE(s_NvOdmWakeupPadInfo);

    return (const NvOdmWakeupPadInfo *) s_NvOdmWakeupPadInfo;
}

const NvU8* NvOdmQueryManufacturer(void)
{
    return s_NvOdmQueryManufacturerSetting;
}

const NvU8* NvOdmQueryModel(void)
{
    return s_NvOdmQueryModelSetting;
}

const NvU8* NvOdmQueryPlatform(void)
{
    return s_NvOdmQueryPlatformSetting;
}

const NvU8* NvOdmQueryProjectName(void)
{
    return s_NvOdmQueryProjectNameSetting;
}

#define EXT 0     
#define INT_PU 1  
#define INT_PD 2  

#define HIGHSPEED 1
#define SCHMITT 1
#define VREF    1
#define OHM_50 3
#define OHM_100 2
#define OHM_200 1
#define OHM_400 0

 
static const NvOdmPinAttrib pin_config[] = {
     
    { NvOdmPinRegister_Ap20_PullUpDown_A,
      NVODM_QUERY_PIN_AP20_PULLUPDOWN_A(0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0) },

    
    { NvOdmPinRegister_Ap20_PullUpDown_B,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_B(0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x2, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0) },

    { NvOdmPinRegister_Ap20_PullUpDown_C,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_C(0x1, 0x1, 0x1, 0x1, 0x2, 0x1, 0x2, 0x1, 0x2, 0x2, 0x2, 0x2, 0x0, 0x0, 0x0) },

    { NvOdmPinRegister_Ap20_PullUpDown_D,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_D(0x2, 0x2, 0x0, 0x2, 0x2, 0x2, 0x1, 0x1, 0x1, 0x1, 0x2, 0x2, 0x1, 0x0, 0x2, 0x2) },

    
    { NvOdmPinRegister_Ap20_PullUpDown_E,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_E(0x2, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x2, 0x2) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_AOCFG1PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_AOCFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_SDIO2CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_SDIO3CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_DBGCFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_VICFG1PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_VICFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_DDCCFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_SDIO1CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_ATCFG1PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_DAP1CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_ATCFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) }
};



static const NvOdmPinAttrib pin_config_evt2_2[] = {
     
    { NvOdmPinRegister_Ap20_PullUpDown_A,
      NVODM_QUERY_PIN_AP20_PULLUPDOWN_A(0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0) },

    
    { NvOdmPinRegister_Ap20_PullUpDown_B,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_B(0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x2, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0) },

    { NvOdmPinRegister_Ap20_PullUpDown_C,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_C(0x1, 0x1, 0x1, 0x1, 0x2, 0x1, 0x2, 0x1, 0x2, 0x2, 0x2, 0x2, 0x0, 0x0, 0x0) },

    { NvOdmPinRegister_Ap20_PullUpDown_D,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_D(0x2, 0x2, 0x0, 0x2, 0x2, 0x2, 0x1, 0x1, 0x1, 0x1, 0x2, 0x2, 0x1, 0x0, 0x2, 0x2) },

    
    { NvOdmPinRegister_Ap20_PullUpDown_E,
     NVODM_QUERY_PIN_AP20_PULLUPDOWN_E(0x2, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x2, 0x2) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_AOCFG1PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_AOCFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_SDIO2CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_SDIO3CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_DBGCFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_VICFG1PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_VICFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_DDCCFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_SDIO1CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_ATCFG1PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_ATCFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    { NvOdmPinRegister_Ap20_PadCtrl_DAP1CFGPADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_50, 31, 31, 3, 3) },

    
    { NvOdmPinRegister_Ap20_PadCtrl_LCDCFG2PADCTRL,
      NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(!HIGHSPEED, SCHMITT, OHM_400, 31, 31, 3, 3) }
};


NvU32
NvOdmQueryPinAttributes(const NvOdmPinAttrib** pPinAttributes)
{
    if (pPinAttributes)
    {
    	
    	
        if (system_rev < EVT2_2 || system_rev == HWID_UNKNOWN)
        {
            *pPinAttributes = &pin_config[0];
            return NV_ARRAY_SIZE(pin_config);
        }
        else
        {
            *pPinAttributes = &pin_config_evt2_2[0];
            return NV_ARRAY_SIZE(pin_config_evt2_2);
        }
        
        
        
    }
    return 0;
}

NvBool NvOdmQueryGetPmuProperty(NvOdmPmuProperty* pPmuProperty)
{
    pPmuProperty->IrqConnected = NV_TRUE;
    pPmuProperty->PowerGoodCount = 0x7E7E;
    pPmuProperty->IrqPolarity = NvOdmInterruptPolarity_Low;
    pPmuProperty->CorePowerReqPolarity = NvOdmCorePowerReqPolarity_Low;
    pPmuProperty->SysClockReqPolarity = NvOdmSysClockReqPolarity_High;
    pPmuProperty->CombinedPowerReq = NV_FALSE;
    pPmuProperty->CpuPowerGoodUs = 2000;
    pPmuProperty->AccuracyPercent = 3;
    pPmuProperty->VCpuOTPOnWakeup = NV_FALSE;
    pPmuProperty->PowerOffCount = 0;
    pPmuProperty->CpuPowerOffUs = 0;
    return NV_TRUE;
}


const NvOdmSocPowerStateInfo* NvOdmQueryLowestSocPowerState(void)
{

    static                      NvOdmSocPowerStateInfo  PowerStateInfo;
    const static                NvOdmSocPowerStateInfo* pPowerStateInfo = NULL;
    NvOdmServicesKeyListHandle  hKeyList;
    NvU32                       LPStateSelection = 0;
    if (pPowerStateInfo == NULL)
    {

        hKeyList = NvOdmServicesKeyListOpen();
        if (hKeyList)
        {
            LPStateSelection = NvOdmServicesGetKeyValue(hKeyList,
                                                NvOdmKeyListId_ReservedBctCustomerOption);
            NvOdmServicesKeyListClose(hKeyList);
            LPStateSelection = NV_DRF_VAL(TEGRA_DEVKIT, BCT_CUSTOPT, LPSTATE, LPStateSelection);
        }
        
        PowerStateInfo.LowestPowerState =  ((LPStateSelection != TEGRA_DEVKIT_BCT_CUSTOPT_0_LPSTATE_LP1)?
                                            NvOdmSocPowerState_Suspend : NvOdmSocPowerState_DeepSleep);
        pPowerStateInfo = (const NvOdmSocPowerStateInfo*) &PowerStateInfo;
    }
    return (pPowerStateInfo);
}

const NvOdmUsbProperty*
NvOdmQueryGetUsbProperty(NvOdmIoModule OdmIoModule,
                         NvU32 Instance)
{
    static const NvOdmUsbProperty Usb1Property =
    {
        NvOdmUsbInterfaceType_Utmi,
        (NvOdmUsbChargerType_SE0 | NvOdmUsbChargerType_SE1 | NvOdmUsbChargerType_SK),
        20,
        NV_TRUE,
        NvOdmUsbModeType_Device,
        NvOdmUsbIdPinType_CableId,
        NvOdmUsbConnectorsMuxType_None,
        NV_TRUE
    };

     static const NvOdmUsbProperty Usb2Property =
     {
        NvOdmUsbInterfaceType_UlpiExternalPhy,
        NvOdmUsbChargerType_UsbHost,
        20,
        NV_TRUE,
        NvOdmUsbModeType_None,
        NvOdmUsbIdPinType_None,
        NvOdmUsbConnectorsMuxType_None,
        NV_TRUE
    };

    static const NvOdmUsbProperty Usb3Property =
    {
        NvOdmUsbInterfaceType_Utmi,
        NvOdmUsbChargerType_UsbHost,
        20,
        NV_TRUE,
        NvOdmUsbModeType_Host,
        NvOdmUsbIdPinType_None,
        NvOdmUsbConnectorsMuxType_None,
        NV_TRUE
    };

    if (OdmIoModule == NvOdmIoModule_Usb && Instance == 0)
        return &(Usb1Property);

    if (OdmIoModule == NvOdmIoModule_Usb && Instance == 1)
        return &(Usb2Property);

    if (OdmIoModule == NvOdmIoModule_Usb && Instance == 2)
        return &(Usb3Property);

    return (const NvOdmUsbProperty *)NULL;
}

const NvOdmQuerySdioInterfaceProperty* NvOdmQueryGetSdioInterfaceProperty(NvU32 Instance)
{
    return &s_NvOdmQuerySdioInterfaceProperty[Instance];
}

const NvOdmQueryHsmmcInterfaceProperty* NvOdmQueryGetHsmmcInterfaceProperty(NvU32 Instance)
{
    return NULL;
}

NvU32
NvOdmQueryGetBlockDeviceSectorSize(NvOdmIoModule OdmIoModule)
{
    return 0;
}

const NvOdmQueryOwrDeviceInfo* NvOdmQueryGetOwrDeviceInfo(NvU32 Instance)
{
    return NULL;
}

const NvOdmGpioWakeupSource *NvOdmQueryGetWakeupSources(NvU32 *pCount)
{
    *pCount = 0;
    return NULL;
}

NvU32 NvOdmQueryHiddenMemSize(void)
{
    return 0x800000;
}

NvU32 NvOdmQueryMemSize_Early(void)
{
    return 512*1024*1024;
}


NvU32 NvOdmQueryMemSize(NvOdmMemoryType MemType)
{
    NvOdmOsOsInfo Info;
    NvU32 MemBctCustOpt = GetBctKeyValue();

    switch (MemType)
    {
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        case NvOdmMemoryType_Sdram:
            switch (NV_DRF_VAL(TEGRA_DEVKIT, BCT_SYSTEM, MEMORY, MemBctCustOpt))
            {
                case TEGRA_DEVKIT_BCT_SYSTEM_0_MEMORY_256:
                    if ( NvOdmOsGetOsInformation(&Info) &&
                         ((Info.OsType!=NvOdmOsOs_Windows) ||
                          (Info.OsType==NvOdmOsOs_Windows && Info.MajorVersion>=7)) )
                        return 0x10000000-NvOdmQueryHiddenMemSize();
                    else
                        return 0x0DD00000-NvOdmQueryHiddenMemSize();  

                case TEGRA_DEVKIT_BCT_SYSTEM_0_MEMORY_1024:
                    if ( NvOdmOsGetOsInformation(&Info) &&
                         ((Info.OsType!=NvOdmOsOs_Windows) ||
                          (Info.OsType==NvOdmOsOs_Windows && Info.MajorVersion>=7)) )
                        return 0x40000000-NvOdmQueryHiddenMemSize();
                    else
                        
                        return 0x1E000000-NvOdmQueryHiddenMemSize();  

                case TEGRA_DEVKIT_BCT_SYSTEM_0_MEMORY_512:
                case TEGRA_DEVKIT_BCT_SYSTEM_0_MEMORY_DEFAULT:
                default:
                    if ( NvOdmOsGetOsInformation(&Info) &&
                         ((Info.OsType!=NvOdmOsOs_Windows) ||
                          (Info.OsType==NvOdmOsOs_Windows && Info.MajorVersion>=7)) )
                        return 0x20000000-NvOdmQueryHiddenMemSize();
                    else
                        return 0x1E000000-NvOdmQueryHiddenMemSize();  
            }

        case NvOdmMemoryType_Nor:
            return 0x00400000;  

        case NvOdmMemoryType_Nand:
        case NvOdmMemoryType_I2CEeprom:
        case NvOdmMemoryType_Hsmmc:
        case NvOdmMemoryType_Mio:
        default:
            return 0;
    }
}

NvU32 NvOdmQueryCarveoutSize(void)
{
    return 0x04000000;  
}

NvU32 NvOdmQuerySecureRegionSize(void)
{
    return 0x00800000;
}
