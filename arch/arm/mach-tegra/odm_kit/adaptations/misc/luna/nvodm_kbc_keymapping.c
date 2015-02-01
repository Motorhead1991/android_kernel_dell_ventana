/*
 * Copyright (c) 2009 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "nvodm_kbc_keymapping.h"
#include <linux/input.h>

#define KBC_QWERTY_NORMAL_KEY_CODE_BASE          0x1000
#define KBC_QWERTY_FUNCTION_KEY_CODE_BASE        0x2000

#define KBC_QWERTY_FUNCTION_KEY_ROW_BASE         0x100
#define KBC_QWERTY_FUNCTION_KEY_ROW_NUMBER       0
#define KBC_QWERTY_FUNCTION_KEY_COLUMN_NUMBER    7





#define NV_SOC_NORMAL_KEY_SCAN_CODE_TABLE_FIRST      KBC_QWERTY_NORMAL_KEY_CODE_BASE
#define NV_SOC_NORMAL_KEY_SCAN_CODE_TABLE_LAST       (KBC_QWERTY_NORMAL_KEY_CODE_BASE +0x7F)

#define NV_SOC_FUNCTION_KEY_SCAN_CODE_TABLE_FIRST    KBC_QWERTY_FUNCTION_KEY_CODE_BASE
#define NV_SOC_FUNCTION_KEY_SCAN_CODE_TABLE_LAST     (KBC_QWERTY_FUNCTION_KEY_CODE_BASE +0x7F)



#define KEY_UNUSED 0

static NvU32 ScanCodeToVKeyTableKbcQwertyNormal[] =
{
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
       KEY_UNUSED,     KEY_UNUSED,     KEY_W,           KEY_S,
                KEY_A,         KEY_Z,         KEY_UNUSED,    KEY_FN,
       KEY_UNUSED,       KEY_UNUSED,     KEY_UNUSED,      KEY_UNUSED,
                KEY_UNUSED,    KEY_UNUSED,    KEY_UNUSED,    KEY_MENU,
       KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,      KEY_UNUSED,
                KEY_UNUSED,    KEY_UNUSED,    KEY_LEFTALT,     KEY_RIGHTALT,
       KEY_5,          KEY_4,          KEY_R,           KEY_E,         
                KEY_F,         KEY_D,         KEY_X,         KEY_UNUSED,
       KEY_7,          KEY_6,          KEY_T,           KEY_H,         
                KEY_G,         KEY_V,         KEY_C,         KEY_SPACE,
       KEY_9,          KEY_8,          KEY_U,           KEY_Y,         
                KEY_J,         KEY_N,         KEY_B,         KEY_BACKSLASH,
       KEY_MINUS,      KEY_0,          KEY_O,           KEY_I,         
                KEY_L,         KEY_K,         KEY_COMMA,     KEY_M,
       KEY_UNUSED,     KEY_EQUAL,      KEY_RIGHTBRACE,    KEY_ENTER,     
                KEY_UNUSED,    KEY_UNUSED,     KEY_UNUSED,     KEY_MENU,
       KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     
                KEY_LEFTSHIFT,     KEY_RIGHTSHIFT,     KEY_UNUSED,     KEY_UNUSED,
       KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     
                KEY_UNUSED,     KEY_LEFTCTRL,    KEY_UNUSED,     KEY_RIGHTCTRL,
       KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     
                KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,     KEY_UNUSED,
       KEY_LEFTBRACE,  KEY_P,          KEY_APOSTROPHE,    KEY_SEMICOLON,    
                KEY_SLASH,    KEY_DOT,    KEY_UNUSED,   KEY_UNUSED,
       KEY_F10,        KEY_F9,         KEY_BACKSPACE,       KEY_3,      
                KEY_2,           KEY_UP,         KEY_PRINT,   KEY_PAUSE,
       KEY_INSERT,     KEY_DELETE,     KEY_UNUSED,     KEY_PAGEUP,      
                KEY_PAGEDOWN,       KEY_RIGHT,      KEY_DOWN,       KEY_LEFT,
       KEY_F11,        KEY_F12,        KEY_F8,         KEY_Q,           
                KEY_F4,         KEY_F3,         KEY_1,           KEY_F7,
       KEY_ESC,        KEY_GRAVE,      KEY_F5,         KEY_TAB,         
                KEY_F1,         KEY_F2,          KEY_CAPSLOCK ,    KEY_F6
};

static NvU32 ScanCodeToVKeyTableKbcQwertyFunction[] =
{
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_7,       KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_9,       KEY_8,       KEY_4,       KEY_UNUSED,  KEY_1,       KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_SLASH,   KEY_6,       KEY_5,       KEY_3,       KEY_2,       KEY_UNUSED,  KEY_0,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_KPASTERISK,  KEY_UNUSED,  KEY_KPMINUS,  KEY_KPPLUS,  KEY_DOT,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_VOLUMEUP,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_HOME,    KEY_END,     KEY_BRIGHTNESSUP,  KEY_VOLUMEDOWN,  KEY_BRIGHTNESSDOWN,
    KEY_NUMLOCK, KEY_SCROLLLOCK,  KEY_MUTE,KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,
    KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED,  KEY_QUESTION,KEY_UNUSED,  KEY_UNUSED,  KEY_UNUSED
};
static struct NvOdmKeyVirtTableDetail s_ScvkQwertyNormalEngUS =
{
    NV_SOC_NORMAL_KEY_SCAN_CODE_TABLE_FIRST,    
    NV_SOC_NORMAL_KEY_SCAN_CODE_TABLE_LAST,     
    ScanCodeToVKeyTableKbcQwertyNormal          
};

static struct NvOdmKeyVirtTableDetail s_ScvkQwertyFunctionEngUS =
{
    NV_SOC_FUNCTION_KEY_SCAN_CODE_TABLE_FIRST,      
    NV_SOC_FUNCTION_KEY_SCAN_CODE_TABLE_LAST,       
    ScanCodeToVKeyTableKbcQwertyFunction            
};

static const struct NvOdmKeyVirtTableDetail *s_pVirtualKeyTables[] =
     {&s_ScvkQwertyNormalEngUS, &s_ScvkQwertyFunctionEngUS};


NvU32
NvOdmKbcKeyMappingGetVirtualKeyMappingList(
    const struct NvOdmKeyVirtTableDetail ***pVirtKeyTableList)
{
   *pVirtKeyTableList = s_pVirtualKeyTables;
   return NV_ARRAY_SIZE(s_pVirtualKeyTables);
}
