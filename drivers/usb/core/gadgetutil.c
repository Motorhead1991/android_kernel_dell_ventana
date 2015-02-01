/*
 * gadgetutil.c
 *
 * Copyright (C) 2010 by Franko Fang <huananhu@huawei.com> (Huawei Technologies Co., Ltd.)
 *
 * Released under the GPLv2.
 *
 */


#include <linux/usb.h>
#include "gadgetutil.h"


struct gadget_util gadgetutil = {
	NULL,
	NULL,
	NULL,
	NULL
};
int bridge_mode = 1;
int product_id = -1;

typedef struct __pid_port_index_map
{
	ushort pid;
	ushort port_index[3];
}pid_pindex_map, *pid_pindex_map_ptr;

static pid_pindex_map pid_map[] = {
	{0x1001, {0x0, 0x1, 0x2}},
	{0x1003, {0x0, 0xf, 0x1}},
	{0x1404, {0x0, 0x1, 0x2}},
	{0x140C, {0x0, 0x2, 0x3}},
	{0xffff, {0xf, 0xf, 0xf}}
};

int gadget_get_map_index_device_to_bridge(int pid, int src_idx) {
	int idx = 0;
	int index = 0xf;
	while (0xffff != pid_map[idx].pid) {
		if (pid == pid_map[idx].pid) {
			for (index = 0; index < 3; index++) {
				if (src_idx == pid_map[idx].port_index[index]) {
					
					return index;
				}
			}
		}
		idx++;
	}
	return index;
}
int gadget_get_map_index_bridge_to_device(int pid, int src_idx)
{
	int index = 0xf;
	int idx = 0;
	while (0xffff != pid_map[idx].pid) {
		if (pid == pid_map[idx].pid) {
			index = pid_map[idx].port_index[src_idx];
		}
		idx++;
	}
	return index;
}
int gadget_get_diag_intfnum(int pid)
{
	int idx = 0;
	while (0xffff != pid_map[idx].pid) {
		if (pid == pid_map[idx].pid) {
			return pid_map[idx].port_index[0];
			
		}
		idx++;
	}
	return 0xf;
}


EXPORT_SYMBOL(gadgetutil);
EXPORT_SYMBOL(bridge_mode);
EXPORT_SYMBOL(product_id);
EXPORT_SYMBOL(gadget_get_map_index_device_to_bridge);
EXPORT_SYMBOL(gadget_get_map_index_bridge_to_device);
EXPORT_SYMBOL(gadget_get_diag_intfnum);
