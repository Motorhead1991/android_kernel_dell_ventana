/*
 * gadgetutil.h
 *
 * Copyright (C) 2010 by Franko Fang <huananhu@huawei.com> (Huawei Technologies Co., Ltd.)
 *
 * Released under the GPLv2.
 *
 */

int gadget_get_map_index_device_to_bridge(int pid, int src_idx) ;
int gadget_get_map_index_bridge_to_device(int pid, int src_idx);
int gadget_get_diag_intfnum(int pid);


struct gadget_util
{
	int 	(*bridge_copyto_us)(unsigned int serial_index, char *packet, unsigned int size);
	int 	(*bridge_copyto_gs)(unsigned int port_index, char *packet, unsigned int size);
	void 	(*bridge_start_read_from_us)(void);
	void 	(*bridge_stop_read_from_us)(void);
};

extern struct gadget_util gadgetutil;
extern int bridge_mode;
extern int product_id;
extern int bridge_enabled; 

