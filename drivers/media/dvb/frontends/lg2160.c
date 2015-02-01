/*
 *    Support for LG2160 - ATSC/MH
 *
 *    Copyright (C) 2010 Michael Krufky <mkrufky@linuxtv.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#if 0
#include <asm/div64.h>
#include "compat.h"
#endif
#include <linux/dvb/frontend.h>
#if 0
#include "dvb_math.h"
#endif
#include "lg2160.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, reg=2 (or-able))");

#define DBG_INFO 1
#define DBG_REG  2

#define lg_printk(kern, fmt, arg...)					\
	printk(kern "%s: " fmt, __func__, ##arg)

#define lg_info(fmt, arg...)	printk(KERN_INFO "lg2160: " fmt, ##arg)
#define lg_warn(fmt, arg...)	lg_printk(KERN_WARNING,       fmt, ##arg)
#define lg_err(fmt, arg...)	lg_printk(KERN_ERR,           fmt, ##arg)
#define lg_dbg(fmt, arg...) if (debug & DBG_INFO)			\
				lg_printk(KERN_DEBUG,         fmt, ##arg)
#define lg_reg(fmt, arg...) if (debug & DBG_REG)			\
				lg_printk(KERN_DEBUG,         fmt, ##arg)

#define lg_fail(ret)							\
({									\
	int __ret;							\
	__ret = (ret < 0);						\
	if (__ret)							\
		lg_err("error %d on line %d\n",	ret, __LINE__);		\
	__ret;								\
})

struct lg216x_state {
	struct i2c_adapter *i2c_adap;
	const struct lg2160_config *cfg;

	struct dvb_frontend frontend;

	u32 current_frequency;
	u8 parade_id;
	u8 fic_ver;
};



static int lg216x_write_reg(struct lg216x_state *state, u16 reg, u8 val)
{
	int ret;
	u8 buf[] = { reg >> 8, reg & 0xff, val };
	struct i2c_msg msg = {
		.addr = state->cfg->i2c_addr, .flags = 0,
		.buf = buf, .len = 3,
	};

	lg_reg("reg: 0x%04x, val: 0x%02x\n", reg, val);

	ret = i2c_transfer(state->i2c_adap, &msg, 1);

	if (ret != 1) {
		lg_err("error (addr %02x %02x <- %02x, err = %i)\n",
		       msg.buf[0], msg.buf[1], msg.buf[2], ret);
		if (ret < 0)
			return ret;
		else
			return -EREMOTEIO;
	}
	return 0;
}

static int lg216x_read_reg(struct lg216x_state *state, u16 reg, u8 *val)
{
	int ret;
	u8 reg_buf[] = { reg >> 8, reg & 0xff };
	struct i2c_msg msg[] = {
		{ .addr = state->cfg->i2c_addr,
		  .flags = 0, .buf = reg_buf, .len = 2 },
		{ .addr = state->cfg->i2c_addr,
		  .flags = I2C_M_RD, .buf = val, .len = 1 },
	};

	lg_reg("reg: 0x%04x\n", reg);

	ret = i2c_transfer(state->i2c_adap, msg, 2);

	if (ret != 2) {
		lg_err("error (addr %02x reg %04x error (ret == %i)\n",
		       state->cfg->i2c_addr, reg, ret);
		if (ret < 0)
			return ret;
		else
			return -EREMOTEIO;
	}
	return 0;
}

#if 0
#define read_reg(state, reg)						\
({									\
	u8 __val;							\
	int ret = lg216x_read_reg(state, reg, &__val);		\
	if (lg_fail(ret))						\
		__val = 0;						\
	__val;								\
})

struct lg216x_reg {
	u16 reg;
	u8 val;
};

static int lg216x_write_regs(struct lg216x_state *state,
			     struct lg216x_reg *regs, int len)
{
	int i, ret;

	lg_reg("writing %d registers...\n", len);

	for (i = 0; i < len - 1; i++) {
		ret = lg216x_write_reg(state, regs[i].reg, regs[i].val);
		if (lg_fail(ret))
			return ret;
	}
	return 0;
}
#endif

static int lg216x_set_reg_bit(struct lg216x_state *state,
			      u16 reg, int bit, int onoff)
{
	u8 val;
	int ret;

	lg_reg("reg: 0x%04x, bit: %d, level: %d\n", reg, bit, onoff);

	ret = lg216x_read_reg(state, reg, &val);
	if (lg_fail(ret))
		goto fail;

	val &= ~(1 << bit);
	val |= (onoff & 1) << bit;

	ret = lg216x_write_reg(state, reg, val);
	lg_fail(ret);
fail:
	return ret;
}



static int lg216x_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct lg216x_state *state = fe->demodulator_priv;
	int ret;

	if (state->cfg->deny_i2c_rptr)
		return 0;

	lg_dbg("(%d)\n", enable);

	ret = lg216x_set_reg_bit(state, 0x0000, 0, enable ? 0 : 1);

	msleep(1);

	return ret;
}

static int lg216x_soft_reset(struct lg216x_state *state)
{
	int ret;

	lg_dbg("\n");

	ret = lg216x_write_reg(state, 0x0002, 0x00);
	if (lg_fail(ret))
		goto fail;

	msleep(20);
	ret = lg216x_write_reg(state, 0x0002, 0x01);
	lg_fail(ret);
fail:
	return ret;
}



static int lg216x_set_if(struct lg216x_state *state)
{
	u8 val;
	int ret;

	lg_dbg("%d KHz\n", state->cfg->if_khz);

	ret = lg216x_read_reg(state, 0x0132, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xfb;
	val |= (0 == state->cfg->if_khz) ? 0x04 : 0x00;

	ret = lg216x_write_reg(state, 0x0132, val);
	lg_fail(ret);

	
fail:
	return ret;
}



static int lg2160_agc_fix(struct lg216x_state *state,
			  int if_agc_fix, int rf_agc_fix)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0100, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xf3;
	val |= (if_agc_fix) ? 0x08 : 0x00;
	val |= (rf_agc_fix) ? 0x04 : 0x00;

	ret = lg216x_write_reg(state, 0x0100, val);
	lg_fail(ret);
fail:
	return ret;
}

#if 0
static int lg2160_agc_freeze(struct lg216x_state *state,
			     int if_agc_freeze, int rf_agc_freeze)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0100, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xcf;
	val |= (if_agc_freeze) ? 0x20 : 0x00;
	val |= (rf_agc_freeze) ? 0x10 : 0x00;

	ret = lg216x_write_reg(state, 0x0100, val);
	lg_fail(ret);
fail:
	return ret;
}
#endif

static int lg2160_agc_polarity(struct lg216x_state *state,
			       int if_agc_polarity, int rf_agc_polarity)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0100, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xfc;
	val |= (if_agc_polarity) ? 0x02 : 0x00;
	val |= (rf_agc_polarity) ? 0x01 : 0x00;

	ret = lg216x_write_reg(state, 0x0100, val);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2160_tuner_pwr_save_polarity(struct lg216x_state *state,
					  int polarity)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0008, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xfe;
	val |= (polarity) ? 0x01 : 0x00;

	ret = lg216x_write_reg(state, 0x0008, val);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2160_spectrum_polarity(struct lg216x_state *state,
				    int inverted)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0132, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xfd;
	val |= (inverted) ? 0x02 : 0x00;

	ret = lg216x_write_reg(state, 0x0132, val);
	lg_fail(ret);
fail:
	return lg216x_soft_reset(state);
}

static int lg2160_tuner_pwr_save(struct lg216x_state *state, int onoff)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0007, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xbf;
	val |= (onoff) ? 0x40 : 0x00;

	ret = lg216x_write_reg(state, 0x0007, val);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2160_initialize(struct lg216x_state *state)
{
	int ret;

#if 0
	ret = lg216x_write_reg(state, 0x0015, 0xe6);
	if (lg_fail(ret))
		goto fail;
#else
	ret = lg216x_write_reg(state, 0x0015, 0xf7);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x001b, 0x52);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0208, 0x00);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0209, 0x82);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0210, 0xf9);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x020a, 0x00);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x020b, 0x82);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x020d, 0x28);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x020f, 0x14);
	if (lg_fail(ret))
		goto fail;
#endif

	ret = lg216x_soft_reset(state);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2161_initialize(struct lg216x_state *state)
{
	int ret;

	ret = lg216x_write_reg(state, 0x0000, 0x41);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0001, 0xfb);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0216, 0x00);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0219, 0x00);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x021b, 0x55);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0606, 0x0a);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_soft_reset(state);
	lg_fail(ret);
fail:
	return ret;
}

static int lg216x_initialize(struct lg216x_state *state)
{
	int ret;
	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg2160_initialize(state);
		break;
	case LG2161:
		ret = lg2161_initialize(state);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int lg2160_set_parade(struct lg216x_state *state, int id)
{
	int ret;

	ret = lg216x_write_reg(state, 0x013e, id & 0x7f);
	if (lg_fail(ret))
		goto fail;

	state->parade_id = id & 0x7f;
fail:
	return ret;
}

static int lg2160_set_ensemble(struct lg216x_state *state, int id)
{
	int ret;
	u16 reg;
	u8 val;

	switch (state->cfg->lg_chip) {
	case LG2160:
		reg = 0x0400;
		break;
	case LG2161:
		reg = 0x0500;
		break;
	}

	ret = lg216x_read_reg(state, reg, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xfe;
	val |= (id) ? 0x01 : 0x00;

	ret = lg216x_write_reg(state, reg, val);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2160_set_spi_clock(struct lg216x_state *state)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0014, &val);
	if (lg_fail(ret))
		goto fail;

	val &= 0xf3;
	val |= (state->cfg->spi_clock << 2);

	ret = lg216x_write_reg(state, 0x0014, val);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2161_set_output_interface(struct lg216x_state *state)
{
	u8 val;
	int ret;

	ret = lg216x_read_reg(state, 0x0014, &val);
	if (lg_fail(ret))
		goto fail;

	val &= ~0x07;
	val |= state->cfg->output_if; 

	ret = lg216x_write_reg(state, 0x0014, val);
	lg_fail(ret);
fail:
	return ret;
}

static int lg2160_enable_fic(struct lg216x_state *state, int onoff)
{
	int ret;

	ret = lg216x_write_reg(state, 0x0017, 0x23);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0016, 0xfc);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_write_reg(state, 0x0016, 0xfc | (onoff) ? 0x02 : 0x00);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_initialize(state);
	if (lg_fail(ret))
		goto fail;

	if (onoff) {
		ret = lg216x_write_reg(state, 0x0017, 0x03);
		if (lg_fail(ret))
			goto fail;
	}

	lg_fail(ret);
fail:
	return ret;
}



enum lg216x_sccc_block_mode {
	LG2160_SEPERATE          = 0,
	LG2160_COMBINDED         = 1,
	LG2160_SCCC_RES          = 2,
};

enum lg216x_sccc_code_mode {
	LG2160_HALF              = 0,
	LG2160_QUATER            = 1,
	LG2160_SCCC_CODE_RES     = 2,
};

enum lg2160_rs_frame_ensemble {
	LG2160_PRIMARY_ENS       = 0,
	LG2160_SECONDARY_ENS     = 1,
};

enum lg2160_rs_frame_mode {
	LG2160_ONLY_PRIMARY      = 0,
	LG2160_PRIMARY_SECONDARY = 1,
	LG2160_RSFRAME_RES       = 2,
};

enum lg2160_rs_code_mode {
	LG2160_211_187           = 0,
	LG2160_223_187           = 1,
	LG2160_235_187           = 2,
	LG2160_RSCODE_RES        = 3,
};



static int lg2160_get_fic_version(struct lg216x_state *state, u8 *ficver)
{
	u8 val;
	int ret;

	*ficver = 0xff; 

	ret = lg216x_read_reg(state, 0x0128, &val);
	if (lg_fail(ret))
		goto fail;

	*ficver = (val >> 3) & 0x1f;
fail:
	return ret;
}

#if 0
static int lg2160_get_parade_id(struct lg216x_state *state, u8 *id)
{
	u8 val;
	int ret;

	*id = 0xff; 

	ret = lg216x_read_reg(state, 0x0123, &val);
	if (lg_fail(ret))
		goto fail;

	*id = val & 0x7f;
fail:
	return ret;
}
#endif

static int lg2160_get_nog(struct lg216x_state *state, u8 *nog)
{
	u8 val;
	int ret;

	*nog = 0xff; 

	ret = lg216x_read_reg(state, 0x0124, &val);
	if (lg_fail(ret))
		goto fail;

	*nog = ((val >> 4) & 0x07) + 1;
fail:
	return ret;
}

static int lg2160_get_tnog(struct lg216x_state *state, u8 *tnog)
{
	u8 val;
	int ret;

	*tnog = 0xff; 

	ret = lg216x_read_reg(state, 0x0125, &val);
	if (lg_fail(ret))
		goto fail;

	*tnog = val & 0x1f;
fail:
	return ret;
}

static int lg2160_get_sgn(struct lg216x_state *state, u8 *sgn)
{
	u8 val;
	int ret;

	*sgn = 0xff; 

	ret = lg216x_read_reg(state, 0x0124, &val);
	if (lg_fail(ret))
		goto fail;

	*sgn = val & 0x0f;
fail:
	return ret;
}

static int lg2160_get_prc(struct lg216x_state *state, u8 *prc)
{
	u8 val;
	int ret;

	*prc = 0xff; 

	ret = lg216x_read_reg(state, 0x0125, &val);
	if (lg_fail(ret))
		goto fail;

	*prc = ((val >> 5) & 0x07) + 1;
fail:
	return ret;
}



static int lg216x_get_rs_frame_mode(struct lg216x_state *state,
				    enum lg2160_rs_frame_mode *rs_framemode)
{
	u8 val;
	int ret;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg216x_read_reg(state, 0x0410, &val);
		break;
	case LG2161:
		ret = lg216x_read_reg(state, 0x0513, &val);
		break;
	default:
		ret = -EINVAL;
	}
	if (lg_fail(ret))
		goto fail;

	switch ((val >> 4) & 0x03) {
#if 1
	default:
#endif
	case 0x00:
		*rs_framemode = LG2160_ONLY_PRIMARY;
		break;
	case 0x01:
		*rs_framemode = LG2160_PRIMARY_SECONDARY;
		break;
#if 0
	default:
		*rs_framemode = LG2160_RSFRAME_RES;
		break;
#endif
	}
fail:
	return ret;
}

static int lg216x_get_rs_frame_ensemble(struct lg216x_state *state,
					enum lg2160_rs_frame_ensemble *rs_frame_ens)
{
	u8 val;
	int ret;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg216x_read_reg(state, 0x0400, &val);
		break;
	case LG2161:
		ret = lg216x_read_reg(state, 0x0500, &val);
		break;
	default:
		ret = -EINVAL;
	}
	if (lg_fail(ret))
		goto fail;

	val &= 0x01;
	*rs_frame_ens = (enum lg2160_rs_frame_ensemble) val;
fail:
	return ret;
}

static int lg216x_get_rs_code_mode(struct lg216x_state *state,
				   enum lg2160_rs_code_mode *rs_code_pri,
				   enum lg2160_rs_code_mode *rs_code_sec)
{
	u8 val;
	int ret;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg216x_read_reg(state, 0x0410, &val);
		break;
	case LG2161:
		ret = lg216x_read_reg(state, 0x0513, &val);
		break;
	default:
		ret = -EINVAL;
	}
	if (lg_fail(ret))
		goto fail;

	*rs_code_pri = (enum lg2160_rs_code_mode) ((val >> 2) & 0x03);
	*rs_code_sec = (enum lg2160_rs_code_mode) (val & 0x03);
fail:
	return ret;
}

static int lg216x_get_sccc_block_mode(struct lg216x_state *state,
				      enum lg216x_sccc_block_mode *sccc_block)
{
	u8 val;
	int ret;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg216x_read_reg(state, 0x0315, &val);
		break;
	case LG2161:
		ret = lg216x_read_reg(state, 0x0511, &val);
		break;
	default:
		ret = -EINVAL;
	}
	if (lg_fail(ret))
		goto fail;

	switch (val & 0x03) {
	case 0x00:
		*sccc_block = LG2160_SEPERATE;
		break;
	case 0x01:
		*sccc_block = LG2160_COMBINDED;
		break;
	default:
		*sccc_block = LG2160_SCCC_RES;
		break;
	}
fail:
	return ret;
}

static int lg216x_get_sccc_code_mode(struct lg216x_state *state,
				     enum lg216x_sccc_code_mode *mode_a,
				     enum lg216x_sccc_code_mode *mode_b,
				     enum lg216x_sccc_code_mode *mode_c,
				     enum lg216x_sccc_code_mode *mode_d)
{
	u8 val;
	int ret;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg216x_read_reg(state, 0x0316, &val);
		break;
	case LG2161:
		ret = lg216x_read_reg(state, 0x0512, &val);
		break;
	default:
		ret = -EINVAL;
	}
	if (lg_fail(ret))
		goto fail;

	switch ((val >> 6) & 0x03) {
	case 0x00:
		*mode_a = LG2160_HALF;
		break;
	case 0x01:
		*mode_a = LG2160_QUATER;
		break;
	default:
		*mode_a = LG2160_SCCC_CODE_RES;
		break;
	}

	switch ((val >> 4) & 0x03) {
	case 0x00:
		*mode_b = LG2160_HALF;
		break;
	case 0x01:
		*mode_b = LG2160_QUATER;
		break;
	default:
		*mode_b = LG2160_SCCC_CODE_RES;
		break;
	}

	switch ((val >> 2) & 0x03) {
	case 0x00:
		*mode_c = LG2160_HALF;
		break;
	case 0x01:
		*mode_c = LG2160_QUATER;
		break;
	default:
		*mode_c = LG2160_SCCC_CODE_RES;
		break;
	}

	switch (val & 0x03) {
	case 0x00:
		*mode_d = LG2160_HALF;
		break;
	case 0x01:
		*mode_d = LG2160_QUATER;
		break;
	default:
		*mode_d = LG2160_SCCC_CODE_RES;
		break;
	}
fail:
	return ret;
}



static int lg216x_read_fic_err_count(struct lg216x_state *state, u8 *err)
{
	u8 fic_err;
	int ret;

	*err = 0;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg216x_read_reg(state, 0x0012, &fic_err);
		break;
	case LG2161:
		ret = lg216x_read_reg(state, 0x001e, &fic_err);
		break;
	}
	if (lg_fail(ret))
		goto fail;

	*err = fic_err;
fail:
	return ret;
}

static int lg2160_read_crc_err_count(struct lg216x_state *state, u16 *err)
{
	u8 crc_err1, crc_err2;
	int ret;

	*err = 0;

	ret = lg216x_read_reg(state, 0x0411, &crc_err1);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_read_reg(state, 0x0412, &crc_err2);
	if (lg_fail(ret))
		goto fail;

	*err = (u16)(((crc_err2 & 0x0f) << 8) | crc_err1);
fail:
	return ret;
}

static int lg2161_read_crc_err_count(struct lg216x_state *state, u16 *err)
{
	u8 crc_err;
	int ret;

	*err = 0;

	ret = lg216x_read_reg(state, 0x0612, &crc_err);
	if (lg_fail(ret))
		goto fail;

	*err = (u16)crc_err;
fail:
	return ret;
}

static int lg216x_read_crc_err_count(struct lg216x_state *state, u16 *err)
{
	int ret;
	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg2160_read_crc_err_count(state, err);
		break;
	case LG2161:
		ret = lg2161_read_crc_err_count(state, err);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int lg2160_read_rs_err_count(struct lg216x_state *state, u16 *err)
{
	u8 rs_err1, rs_err2;
	int ret;

	*err = 0;

	ret = lg216x_read_reg(state, 0x0413, &rs_err1);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_read_reg(state, 0x0414, &rs_err2);
	if (lg_fail(ret))
		goto fail;

	*err = (u16)(((rs_err2 & 0x0f) << 8) | rs_err1);
fail:
	return ret;
}

static int lg2161_read_rs_err_count(struct lg216x_state *state, u16 *err)
{
	u8 rs_err1, rs_err2;
	int ret;

	*err = 0;

	ret = lg216x_read_reg(state, 0x0613, &rs_err1);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_read_reg(state, 0x0614, &rs_err2);
	if (lg_fail(ret))
		goto fail;

	*err = (u16)((rs_err1 << 8) | rs_err2);
fail:
	return ret;
}

static int lg216x_read_rs_err_count(struct lg216x_state *state, u16 *err)
{
	int ret;
	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg2160_read_rs_err_count(state, err);
		break;
	case LG2161:
		ret = lg2161_read_rs_err_count(state, err);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}



static int lg2160_get_frontend(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *param)
{
	struct lg216x_state *state = fe->demodulator_priv;
	int ret;

	lg_dbg("\n");

	param->u.vsb.modulation = VSB_8; 
	param->frequency = state->current_frequency;

	fe->dtv_property_cache.delivery_system = SYS_ATSCMH;

	ret = lg2160_get_fic_version(state,
				     &fe->dtv_property_cache.atscmh_fic_ver);
	if (lg_fail(ret))
		goto fail;
	if (state->fic_ver != fe->dtv_property_cache.atscmh_fic_ver) {
		state->fic_ver = fe->dtv_property_cache.atscmh_fic_ver;

		fe->dtv_property_cache.vendor_i2c_bus_no =
			i2c_adapter_id(state->i2c_adap);
#if 0
		ret = lg2160_get_parade_id(state,
				&fe->dtv_property_cache.atscmh_parade_id);
		if (lg_fail(ret))
			goto fail;
#else

#endif
		ret = lg2160_get_nog(state, &fe->dtv_property_cache.atscmh_nog);
		if (lg_fail(ret))
			goto fail;
		ret = lg2160_get_tnog(state, &fe->dtv_property_cache.atscmh_tnog);
		if (lg_fail(ret))
			goto fail;
		ret = lg2160_get_sgn(state, &fe->dtv_property_cache.atscmh_sgn);
		if (lg_fail(ret))
			goto fail;
		ret = lg2160_get_prc(state, &fe->dtv_property_cache.atscmh_prc);
		if (lg_fail(ret))
			goto fail;

		ret = lg216x_get_rs_frame_mode(state,
			(enum lg2160_rs_frame_mode *)
			&fe->dtv_property_cache.atscmh_rs_frame_mode);
		if (lg_fail(ret))
			goto fail;
		ret = lg216x_get_rs_frame_ensemble(state,
			(enum lg2160_rs_frame_ensemble *)
			&fe->dtv_property_cache.atscmh_rs_frame_ensemble);
		if (lg_fail(ret))
			goto fail;
		ret = lg216x_get_rs_code_mode(state,
			(enum lg2160_rs_code_mode *)
			&fe->dtv_property_cache.atscmh_rs_code_mode_pri,
			(enum lg2160_rs_code_mode *)
			&fe->dtv_property_cache.atscmh_rs_code_mode_sec);
		if (lg_fail(ret))
			goto fail;
		ret = lg216x_get_sccc_block_mode(state,
			(enum lg216x_sccc_block_mode *)
			&fe->dtv_property_cache.atscmh_sccc_block_mode);
		if (lg_fail(ret))
			goto fail;
		ret = lg216x_get_sccc_code_mode(state,
			(enum lg216x_sccc_code_mode *)
			&fe->dtv_property_cache.atscmh_sccc_code_mode_a,
			(enum lg216x_sccc_code_mode *)
			&fe->dtv_property_cache.atscmh_sccc_code_mode_b,
			(enum lg216x_sccc_code_mode *)
			&fe->dtv_property_cache.atscmh_sccc_code_mode_c,
			(enum lg216x_sccc_code_mode *)
			&fe->dtv_property_cache.atscmh_sccc_code_mode_d);
		if (lg_fail(ret))
			goto fail;
	}
	ret = lg216x_read_fic_err_count(state,
				(u8 *)&fe->dtv_property_cache.atscmh_fic_err);
	if (lg_fail(ret))
		goto fail;
	ret = lg216x_read_crc_err_count(state,
				&fe->dtv_property_cache.atscmh_crc_err);
	if (lg_fail(ret))
		goto fail;
	ret = lg216x_read_rs_err_count(state,
				&fe->dtv_property_cache.atscmh_rs_err);
	lg_fail(ret);
fail:
	return ret;
}

static int lg216x_get_property(struct dvb_frontend *fe,
			       struct dtv_property *tvp)
{
	struct dvb_frontend_parameters param;

	return (DTV_ATSCMH_FIC_VER == tvp->cmd) ?
		lg2160_get_frontend(fe, &param) : 0;
}


static int lg2160_set_frontend(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *param)
{
	struct lg216x_state *state = fe->demodulator_priv;
	int ret;

	lg_dbg("(%d, %d)\n", param->frequency, param->u.vsb.modulation);

	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe, param);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
		if (lg_fail(ret))
			goto fail;
		state->current_frequency = param->frequency;
	}

	ret = lg2160_agc_fix(state, 0, 0);
	if (lg_fail(ret))
		goto fail;
	ret = lg2160_agc_polarity(state, 0, 0);
	if (lg_fail(ret))
		goto fail;
	ret = lg2160_tuner_pwr_save_polarity(state, 1);
	if (lg_fail(ret))
		goto fail;
	ret = lg216x_set_if(state); 
	if (lg_fail(ret))
		goto fail;
	ret = lg2160_spectrum_polarity(state, state->cfg->spectral_inversion);
	if (lg_fail(ret))
		goto fail;

	
	ret = lg216x_soft_reset(state);
	if (lg_fail(ret))
		goto fail;

	ret = lg2160_tuner_pwr_save(state, 0);
	if (lg_fail(ret))
		goto fail;

	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg2160_set_spi_clock(state);
		if (lg_fail(ret))
			goto fail;
		break;
	case LG2161:
		ret = lg2161_set_output_interface(state);
		if (lg_fail(ret))
			goto fail;
		break;
	}

	ret = lg2160_set_parade(state, fe->dtv_property_cache.atscmh_parade_id);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_initialize(state);
	if (lg_fail(ret))
		goto fail;

	ret = lg2160_enable_fic(state, 1);
	if (lg_fail(ret))
		goto fail;

	ret = lg2160_set_ensemble(state,
			fe->dtv_property_cache.atscmh_rs_frame_ensemble);
	lg_fail(ret);
#if 0
	if ((fe->dtv_property_cache.delivery_system != SYS_ATSCMH) ||
	    () ||
	    () ||
	    () ||
	    () ||
	    ()) {
	}
#endif
	lg2160_get_frontend(fe, param);
fail:
	return ret;
}



static int lg2160_read_lock_status(struct lg216x_state *state,
				   int *acq_lock, int *sync_lock)
{
	u8 val;
	int ret;

	*acq_lock = 0;
	*sync_lock = 0;

	ret = lg216x_read_reg(state, 0x011b, &val);
	if (lg_fail(ret))
		goto fail;

	*sync_lock = (val & 0x20) ? 0 : 1;
	*acq_lock  = (val & 0x40) ? 0 : 1;
fail:
	return ret;
}

#ifdef USE_LG2161_LOCK_BITS
static int lg2161_read_lock_status(struct lg216x_state *state,
				   int *acq_lock, int *sync_lock)
{
	u8 val;
	int ret;

	*acq_lock = 0;
	*sync_lock = 0;

	ret = lg216x_read_reg(state, 0x0304, &val);
	if (lg_fail(ret))
		goto fail;

	*sync_lock = (val & 0x80) ? 0 : 1;

	ret = lg216x_read_reg(state, 0x011b, &val);
	if (lg_fail(ret))
		goto fail;

	*acq_lock  = (val & 0x40) ? 0 : 1;
fail:
	return ret;
}
#endif

static int lg216x_read_lock_status(struct lg216x_state *state,
				   int *acq_lock, int *sync_lock)
{
#ifdef USE_LG2161_LOCK_BITS
	int ret;
	switch (state->cfg->lg_chip) {
	case LG2160:
		ret = lg2160_read_lock_status(state, acq_lock, sync_lock);
		break;
	case LG2161:
		ret = lg2161_read_lock_status(state, acq_lock, sync_lock);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
#else
	return lg2160_read_lock_status(state, acq_lock, sync_lock);
#endif
}

static int lg216x_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct lg216x_state *state = fe->demodulator_priv;
	int ret, acq_lock, sync_lock;

	*status = 0;

	ret = lg216x_read_lock_status(state, &acq_lock, &sync_lock);
	if (lg_fail(ret))
		goto fail;

	lg_dbg("%s%s\n",
	       acq_lock  ? "SIGNALEXIST " : "",
	       sync_lock ? "SYNCLOCK"     : "");

	if (acq_lock)
		*status |= FE_HAS_SIGNAL;
	if (sync_lock)
		*status |= FE_HAS_SYNC;

	if (*status)
		*status |= FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_LOCK;

fail:
	return ret;
}



static int lg2160_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct lg216x_state *state = fe->demodulator_priv;
	u8 snr1, snr2;
	int ret;

	*snr = 0;

	ret = lg216x_read_reg(state, 0x0202, &snr1);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_read_reg(state, 0x0203, &snr2);
	if (lg_fail(ret))
		goto fail;

	if ((snr1 == 0xba) || (snr2 == 0xdf))
		*snr = 0;
	else
#if 1
	*snr =  ((snr1 >> 4) * 100) + ((snr1 & 0x0f) * 10) + (snr2 >> 4);
#else 
	*snr =  (snr2 | (snr1 << 8));
#endif
fail:
	return ret;
}

static int lg2161_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct lg216x_state *state = fe->demodulator_priv;
	u8 snr1, snr2;
	int ret;

	*snr = 0;

	ret = lg216x_read_reg(state, 0x0302, &snr1);
	if (lg_fail(ret))
		goto fail;

	ret = lg216x_read_reg(state, 0x0303, &snr2);
	if (lg_fail(ret))
		goto fail;

	if ((snr1 == 0xba) || (snr2 == 0xfd))
		*snr = 0;
	else

	*snr =  ((snr1 >> 4) * 100) + ((snr1 & 0x0f) * 10) + (snr2 & 0x0f);
fail:
	return ret;
}

static int lg216x_read_signal_strength(struct dvb_frontend *fe,
				       u16 *strength)
{
#if 0
	
	struct lg216x_state *state = fe->demodulator_priv;
	u16 snr;
	int ret;
#endif
	*strength = 0;
#if 0
	ret = fe->ops.read_snr(fe, &snr);
	if (lg_fail(ret))
		goto fail;
	
	
	if (state->snr >= 8960 * 0x10000)
		*strength = 0xffff;
	else
		*strength = state->snr / 8960;
fail:
	return ret;
#else
	return 0;
#endif
}



static int lg216x_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct lg216x_state *state = fe->demodulator_priv;
	u16 rs_err;
	int ret;

	ret = lg216x_read_rs_err_count(state, &rs_err);
	if (lg_fail(ret))
		goto fail;

	*ucblocks = rs_err;
fail:
	return 0;
}

static int lg216x_get_tune_settings(struct dvb_frontend *fe,
				    struct dvb_frontend_tune_settings
				    *fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 500;
	lg_dbg("\n");
	return 0;
}

static void lg216x_release(struct dvb_frontend *fe)
{
	struct lg216x_state *state = fe->demodulator_priv;
	lg_dbg("\n");
	kfree(state);
}

static struct dvb_frontend_ops lg2160_ops = {
	.info = {
		.name = "LG Electronics LG2160 ATSC/MH Frontend",
		.type               = FE_ATSC,
		.frequency_min      = 54000000,
		.frequency_max      = 858000000,
		.frequency_stepsize = 62500,
		.caps = FE_CAN_8VSB | FE_HAS_EXTENDED_CAPS 
	},
	.i2c_gate_ctrl        = lg216x_i2c_gate_ctrl,
#if 0
	.init                 = lg216x_init,
	.sleep                = lg216x_sleep,
#endif
	.get_property         = lg216x_get_property,

	.set_frontend         = lg2160_set_frontend,
	.get_frontend         = lg2160_get_frontend,
	.get_tune_settings    = lg216x_get_tune_settings,
	.read_status          = lg216x_read_status,
#if 0
	.read_ber             = lg216x_read_ber,
#endif
	.read_signal_strength = lg216x_read_signal_strength,
	.read_snr             = lg2160_read_snr,
	.read_ucblocks        = lg216x_read_ucblocks,
	.release              = lg216x_release,
};

static struct dvb_frontend_ops lg2161_ops = {
	.info = {
		.name = "LG Electronics LG2161 ATSC/MH Frontend",
		.type               = FE_ATSC,
		.frequency_min      = 54000000,
		.frequency_max      = 858000000,
		.frequency_stepsize = 62500,
		.caps = FE_CAN_8VSB | FE_HAS_EXTENDED_CAPS 
	},
	.i2c_gate_ctrl        = lg216x_i2c_gate_ctrl,
#if 0
	.init                 = lg216x_init,
	.sleep                = lg216x_sleep,
#endif
	.get_property         = lg216x_get_property,

	.set_frontend         = lg2160_set_frontend,
	.get_frontend         = lg2160_get_frontend,
	.get_tune_settings    = lg216x_get_tune_settings,
	.read_status          = lg216x_read_status,
#if 0
	.read_ber             = lg216x_read_ber,
#endif
	.read_signal_strength = lg216x_read_signal_strength,
	.read_snr             = lg2161_read_snr,
	.read_ucblocks        = lg216x_read_ucblocks,
	.release              = lg216x_release,
};

struct dvb_frontend *lg2160_attach(const struct lg2160_config *config,
				   struct i2c_adapter *i2c_adap)
{
	struct lg216x_state *state = NULL;

	lg_dbg("(%d-%04x)\n",
	       i2c_adap ? i2c_adapter_id(i2c_adap) : 0,
	       config ? config->i2c_addr : 0);

	state = kzalloc(sizeof(struct lg216x_state), GFP_KERNEL);
	if (state == NULL)
		goto fail;

	state->cfg = config;
	state->i2c_adap = i2c_adap;
	state->fic_ver = 0xff;
	state->parade_id = 0xff;

	switch (config->lg_chip) {
	default:
		lg_warn("invalid chip requested, defaulting to LG2160");
		
	case LG2160:
		memcpy(&state->frontend.ops, &lg2160_ops,
		       sizeof(struct dvb_frontend_ops));
		break;
	case LG2161:
		memcpy(&state->frontend.ops, &lg2161_ops,
		       sizeof(struct dvb_frontend_ops));
		break;
	}

	state->frontend.demodulator_priv = state;
	state->current_frequency = -1;
	
	state->frontend.dtv_property_cache.atscmh_parade_id = 1;

	return &state->frontend;
fail:
	lg_warn("unable to detect LG216x hardware\n");
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(lg2160_attach);

MODULE_DESCRIPTION("LG Electronics LG216x ATSC/MH Demodulator Driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");


