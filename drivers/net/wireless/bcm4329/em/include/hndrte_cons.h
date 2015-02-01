/*
 * Console support for hndrte.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: hndrte_cons.h,v 13.1.2.4 2010/07/15 19:06:11 Exp $
 */

#include <typedefs.h>

#ifdef RWL_DONGLE

#define RWL_MAX_DATA_LEN	(512 + 8)	
#define CBUF_LEN	(RWL_MAX_DATA_LEN + 64)  
#else
#define CBUF_LEN	(128)
#endif

#ifdef BCMDBG
#define LOG_BUF_LEN	(16 * 1024)
#else
#ifdef WLLMAC
#define LOG_BUF_LEN	2048
#else 
#define LOG_BUF_LEN	1024
#endif 
#endif 

typedef struct {
	uint32		buf;		
	uint		buf_size;
	uint		idx;
	char		*_buf_compat;	
} hndrte_log_t;

typedef struct {
	
	volatile uint	vcons_in;
	volatile uint	vcons_out;

	
	hndrte_log_t	log;

	
	uint		cbuf_idx;
	char		cbuf[CBUF_LEN];
} hndrte_cons_t;
