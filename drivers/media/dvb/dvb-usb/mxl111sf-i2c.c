/*
 *  mxl111sf-i2c.c - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010 Michael Krufky <mkrufky@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "mxl111sf-i2c.h"
#include "mxl111sf.h"



#define SW_I2C_ADDR		0x1a
#define SW_I2C_EN		0x02
#define SW_SCL_OUT		0x04
#define SW_SDA_OUT		0x08
#define SW_SDA_IN		0x04

#define SW_I2C_BUSY_ADDR	0x2f
#define SW_I2C_BUSY		0x02

static int mxl111sf_i2c_bitbang_sendbyte(struct mxl111sf_state *state,
					 u8 byte)
{
	int i, ret;
	u8 data = 0;

	mxl_i2c("(0x%02x)", byte);

	ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &data);
	if (mxl_fail(ret))
		goto fail;

	for(i = 0; i < 8; i++) {

		data = (byte & (0x80 >> i)) ? SW_SDA_OUT : 0;

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | data);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | data | SW_SCL_OUT);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | data);
		if (mxl_fail(ret))
			goto fail;
	}

	
	if (!(byte & 1)) {
		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | SW_SDA_OUT);
		if (mxl_fail(ret))
			goto fail;
	}

	
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &data);
	if (mxl_fail(ret))
		goto fail;

	
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	if (data & SW_SDA_IN)
		ret = -EIO;
fail:
	return ret;
}

static int mxl111sf_i2c_bitbang_recvbyte(struct mxl111sf_state *state,
					 u8 *pbyte)
{
	int i, ret;
	u8 byte, data = 0;

	mxl_i2c("()");

	*pbyte = 0;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	for(i = 0; i < 8; i++) {
		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN |
					 SW_SCL_OUT | SW_SDA_OUT);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &data);
		if (mxl_fail(ret))
			goto fail;

		if (data & SW_SDA_IN)
			byte |= (0x80 >> i);

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | SW_SDA_OUT);
		if (mxl_fail(ret))
			goto fail;
	}
	*pbyte = byte;
fail:
	return ret;
}

static int mxl111sf_i2c_start(struct mxl111sf_state *state)
{
	int ret;

	mxl_i2c("()");

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN); 
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl111sf_i2c_stop(struct mxl111sf_state *state)
{
	int ret;

	mxl_i2c("()");

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN); 
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_SCL_OUT | SW_SDA_OUT);
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl111sf_i2c_ack(struct mxl111sf_state *state)
{
	int ret;
	u8 b = 0;

	mxl_i2c("()");

	ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &b);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN);
	if (mxl_fail(ret))
		goto fail;

	
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl111sf_i2c_nack(struct mxl111sf_state *state)
{
	int ret;

	mxl_i2c("()");

	
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	mxl_fail(ret);
fail:
	return ret;
}



static int mxl111sf_i2c_sw_xfer_msg(struct mxl111sf_state *state,
				    struct i2c_msg *msg)
{
	int i, ret;

	mxl_i2c("()");

	if (msg->flags & I2C_M_RD) {

		ret = mxl111sf_i2c_start(state);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_i2c_bitbang_sendbyte(state, (msg->addr << 1) | 0x01);
		if (mxl_fail(ret)) {
			mxl111sf_i2c_stop(state);
			goto fail;
		}

		for (i = 0; i < msg->len; i++) {
			ret = mxl111sf_i2c_bitbang_recvbyte(state,
							    &msg->buf[i]);
			if (mxl_fail(ret)) {
				mxl111sf_i2c_stop(state);
				goto fail;
			}

			if (i < msg->len - 1)
				mxl111sf_i2c_ack(state);
		}

		mxl111sf_i2c_nack(state);

		ret = mxl111sf_i2c_stop(state);
		if (mxl_fail(ret))
			goto fail;

	} else {

		ret = mxl111sf_i2c_start(state);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_i2c_bitbang_sendbyte(state, (msg->addr << 1) & 0xfe);
		if (mxl_fail(ret)) {
			mxl111sf_i2c_stop(state);
			goto fail;
		}

		for (i = 0; i < msg->len; i++) {
			ret = mxl111sf_i2c_bitbang_sendbyte(state,
							    msg->buf[i]);
			if (mxl_fail(ret)) {
				mxl111sf_i2c_stop(state);
				goto fail;
			}
		}

		
		mxl111sf_i2c_stop(state);
	}
fail:
	return ret;
}



#define USB_WRITE_I2C_CMD     0x99
#define USB_READ_I2C_CMD      0xdd
#define USB_END_I2C_CMD       0xfe

#define USB_WRITE_I2C_CMD_LEN   26
#define USB_READ_I2C_CMD_LEN    24

#define I2C_MUX_REG           0x30
#define I2C_CONTROL_REG       0x00
#define I2C_SLAVE_ADDR_REG    0x08
#define I2C_DATA_REG          0x0c
#define I2C_INT_STATUS_REG    0x10

#define MXL_USB_DEBUG0(arg...) {}

int mxl111sf_ctrl_msg(struct dvb_usb_device *d,
		      u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen);

static int mxl111sf_i2c_send_data(struct mxl111sf_state *state,
				  u8 index, u8 *wdata)
{
	int ret = mxl111sf_ctrl_msg(state->d, wdata[0],
				    &wdata[1], 25, NULL, 0);
	mxl_fail(ret);

	return ret;
}

static int mxl111sf_i2c_get_data(struct mxl111sf_state *state,
				 u8 index, u8 *wdata, u8 *rdata)
{
	int ret = mxl111sf_ctrl_msg(state->d, wdata[0],
				    &wdata[1], 25, rdata, 24);
	mxl_fail(ret);

	return ret;
}

static u8 mxl111sf_i2c_check_status(struct mxl111sf_state *state)
{
	u8 status = 0;
	u8 buf[26];

	mxl_i2c_adv("()");

	buf[0] = USB_READ_I2C_CMD;
	buf[1] = 0x00;

	buf[2] = I2C_INT_STATUS_REG;
	buf[3] = 0x00;
	buf[4] = 0x00;

	buf[5] = USB_END_I2C_CMD;

	mxl111sf_i2c_get_data(state, 0, buf, buf);

	if (buf[1] & 0x04) {
		status = 1;
	}

	return status;
}

static u8 mxl111sf_i2c_check_fifo(struct mxl111sf_state *state)
{
	u8 status = 0;
	u8 buf[26];

	mxl_i2c("()");

	buf[0] = USB_READ_I2C_CMD;
	buf[1] = 0x00;

	buf[2] = I2C_MUX_REG;
	buf[3] = 0x00;
	buf[4] = 0x00;

	buf[5] = I2C_INT_STATUS_REG;
	buf[6] = 0x00;
	buf[7] = 0x00;
	buf[8] = USB_END_I2C_CMD;

	mxl111sf_i2c_get_data(state, 0, buf, buf);

	if (0x08 == (buf[1] & 0x08)) {
		status = 1;
	}
	if ((buf[5] & 0x02) == 0x02) {
		
	}

	return status;
}

static int mxl111sf_i2c_readagain(struct mxl111sf_state *state,
				  u8 count, u8 *rbuf)
{
	u8 i2c_w_data[26];
	u8 i2c_r_data[24];
	u8 i = 0;
	u8 fifoStatus = 0;
	int ret;
	int status = 0;

	mxl_i2c("read %d bytes", count);

	while ((fifoStatus == 0) && (i++ < 5)) {
		fifoStatus = mxl111sf_i2c_check_fifo(state);
	}

	i2c_w_data[0] = 0xDD;
	i2c_w_data[1] = 0x00;

	for (i = 2; i < 26; i++)
		i2c_w_data[i] = 0xFE;

	for (i = 0; i < count; i++) {
		i2c_w_data[2+(i*3)] = 0x0C;
		i2c_w_data[3+(i*3)] = 0x00;
		i2c_w_data[4+(i*3)] = 0x00;
	}

	ret = mxl111sf_i2c_get_data(state, 0, i2c_w_data, i2c_r_data);

	
	if (mxl111sf_i2c_check_status(state) == 1) {
		MXL_USB_DEBUG0( __FUNCTION__ " Error in re-Read!!!!");
	} else {
		for (i = 0; i < count; i++) {
			rbuf[i] = i2c_r_data[(i*3)+1];
			MXL_USB_DEBUG0("readAgain : %02X\t %02X",
				       i2c_r_data[(i*3)+1],
				       i2c_r_data[(i*3)+2]);
		}

		status = 1;
	}

	return status;
}

#define HWI2C400 1
static int mxl111sf_i2c_hw_xfer_msg(struct mxl111sf_state *state,
				    struct i2c_msg *msg)
{
	int i, k, ret = 0;
	u16 Index = 0;
	u8 buf[26];
	u8 i2c_r_data[24];
	u16 BlockLen;
	u16 LeftOverLen;
	u8 rdStatus[8];
	u8 retStatus;
	u8 readBuff[26];



	mxl_i2c("addr: 0x%02x, read buff len: %d, write buff len: %d",
		  msg->addr, (msg->flags & I2C_M_RD) ? msg->len : 0,
		  (!msg->flags & I2C_M_RD) ? msg->len : 0);

	for (Index = 0; Index < 26; Index++)
		buf[Index] = USB_END_I2C_CMD;

	
	buf[0] = USB_WRITE_I2C_CMD;
	buf[1] = 0x00;

	
	buf[2] = I2C_MUX_REG;
	buf[3] = 0x80;
	buf[4] = 0x00;

	
	buf[5] = I2C_MUX_REG;
	buf[6] = 0x81;
	buf[7] = 0x00;

	
	buf[8] = 0x14;
	buf[9] = 0xff;
	buf[10] = 0x00;

	
	
	
	

	buf[11] = 0x24;
	buf[12] = 0xF7;
	buf[13] = 0x00;

	ret = mxl111sf_i2c_send_data(state, 0, buf);

	
	if ((!msg->flags & I2C_M_RD) && (msg->len > 0)) {
		MXL_USB_DEBUG0(__FUNCTION__ " WriteBuffLen Len %d\t%02X",
			       msg->len, msg->buf[0]);

		
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0x5E;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

		
		buf[5] = I2C_SLAVE_ADDR_REG;
		buf[6] = (msg->addr);
		buf[7] = 0x00;
		buf[8] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_send_data(state, 0, buf);

		
		if (mxl111sf_i2c_check_status(state) == 1) {
			MXL_USB_DEBUG0(__FUNCTION__
				       " Write NACK for Slave Address %x",
				       msg->addr);
			
			buf[2] = I2C_CONTROL_REG;
			buf[3] = 0x4E;
			buf[4] = (HWI2C400) ? 0x03 : 0x0D;
			ret = -EIO;
			goto EXIT;
		}

		
		BlockLen = (msg->len / 8);
		LeftOverLen = (msg->len % 8);
		Index = 0;

		MXL_USB_DEBUG0( __FUNCTION__ " Block Len %d", BlockLen);
		MXL_USB_DEBUG0( __FUNCTION__ " LeftOverLen %d", LeftOverLen);

		for (Index = 0; Index < BlockLen; Index++) {
			for (i = 0; i < 8; i++) {
				
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = msg->buf[(Index*8)+i];
				buf[4+(i*3)] = 0x00;
			}

			ret = mxl111sf_i2c_send_data(state, 0, buf);

			
			if (mxl111sf_i2c_check_status(state) == 1) {
				MXL_USB_DEBUG0(__FUNCTION__
					       " Write NACK for Slave Address %x",
					       msg->addr);

				
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0x4E;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto EXIT;
			}

		}

		if (LeftOverLen) {
			for (k = 0; k < 26; k++)
				buf[k] = USB_END_I2C_CMD;

			buf[0] = 0x99;
			buf[1] = 0x00;

			for (i = 0; i < LeftOverLen; i++) {
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = msg->buf[(Index*8)+i];
				MXL_USB_DEBUG0(__FUNCTION__
					       " Index = %d %d Data %d",
					       Index, i,
					       msg->buf[(Index*8)+i]);
				buf[4+(i*3)] = 0x00;
				
			}
			
			ret = mxl111sf_i2c_send_data(state, 0, buf);

			
			if (mxl111sf_i2c_check_status(state) == 1) {
				MXL_USB_DEBUG0(__FUNCTION__
					       " Write NACK for Slave Address %x",
					       msg->addr);

				
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0x4E;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto EXIT;
			}

		}

		
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0x4E;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

	}

	
	if ((msg->flags & I2C_M_RD) && (msg->len > 0)) {
		MXL_USB_DEBUG0(__FUNCTION__
			       " Read ReadBuff Len %d", msg->len);

		
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0xDF;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

		
		buf[5] = 0x14;
		buf[6] = (msg->len & 0xFF);
		buf[7] = 0;

		
		buf[8] = I2C_SLAVE_ADDR_REG;
		buf[9] = msg->addr;
		buf[10] = 0x00;
		buf[11] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_send_data(state, 0, buf);

		
		if (mxl111sf_i2c_check_status(state) == 1) {
			MXL_USB_DEBUG0(__FUNCTION__
				       " Read NACK for Slave Address %x",
				       msg->addr);

			
			buf[2] = I2C_CONTROL_REG;
			buf[3] = 0xC7;
			buf[4] = (HWI2C400) ? 0x03 : 0x0D;
			ret = -EIO;
			goto EXIT;
		}

		
		BlockLen = ((msg->len) / 8);
		LeftOverLen = ((msg->len) % 8);
		Index = 0;

		MXL_USB_DEBUG0( __FUNCTION__ " Block Len %d", BlockLen);
		MXL_USB_DEBUG0( __FUNCTION__ " LeftOverLen %d", LeftOverLen);

		
		buf[0] = USB_READ_I2C_CMD;
		buf[1] = 0x00;

		for (Index = 0; Index < BlockLen; Index++) {
			
			for (i = 0; i < 8; i++) {
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = 0x00;
				buf[4+(i*3)] = 0x00;
			}

			ret = mxl111sf_i2c_get_data(state, 0, buf, i2c_r_data);

			
			if (mxl111sf_i2c_check_status(state) == 1) {
				MXL_USB_DEBUG0(__FUNCTION__
					       " Read NACK for Slave Address %x",
					       msg->addr);

				
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0xC7;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto EXIT;
			}

			
			for (i = 0; i < 8; i++) {
				rdStatus[i] = i2c_r_data[(i*3)+2];

				if (rdStatus[i] == 0x04) {
					if (i < 7) {
						MXL_USB_DEBUG0(__FUNCTION__
							       " I2C FIFO Empty!!! @ %d", i);
						
						msg->buf[(Index*8)+i] = i2c_r_data[(i*3)+1];
						
						retStatus =
							mxl111sf_i2c_readagain(state, 8-(i+1), readBuff);
						if (retStatus == 1) {
							for(k = 0; k < 8-(i+1); k++) {
								msg->buf[(Index*8)+(k+i+1)] = readBuff[k];
								MXL_USB_DEBUG0("Read data : %02X\t %d", msg->buf[(Index*8)+(k+i)], (Index*8)+(k+i));
								MXL_USB_DEBUG0("Read data : %02X\t %02X", msg->buf[(Index*8)+(k+i+1)], readBuff[k]);
								
							}
							goto STOP_COPY;
						} else {
							MXL_USB_DEBUG0("Read Again ERROR!");
						}
					} else
						msg->buf[(Index*8)+i] = i2c_r_data[(i*3)+1];

				} else
					msg->buf[(Index*8)+i] = i2c_r_data[(i*3)+1];

				
			}
		STOP_COPY:
			;

		}

		if (LeftOverLen) {
			for (k = 0; k < 26; k++)
				buf[k] = USB_END_I2C_CMD;

			buf[0] = 0xDD;
			buf[1] = 0x00;

			for (i = 0; i < LeftOverLen; i++) {
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = 0x00;
				buf[4+(i*3)] = 0x00;
				
			}
			ret = mxl111sf_i2c_get_data(state, 0, buf, i2c_r_data);

			
			if (mxl111sf_i2c_check_status(state) == 1) {
				MXL_USB_DEBUG0(__FUNCTION__
					       " Read NACK for Slave Address %x",
					       msg->addr);

				
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0xC7;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto EXIT;
			}

			for (i = 0; i < LeftOverLen; i++) {
				msg->buf[(BlockLen*8)+i] = i2c_r_data[(i*3)+1];
				MXL_USB_DEBUG0("Read data : %02X\t %02X", i2c_r_data[(i*3)+1], i2c_r_data[(i*3)+2]);
			}
		}

		
		buf[0] = USB_WRITE_I2C_CMD;
		buf[1] = 0x00;

		
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0x17;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

		buf[5] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_send_data(state, 0, buf);

#if 0
		
		buf[0] = USB_READ_I2C_CMD;
		buf[1] = 0x00;

		
		buf[2] = I2C_DATA_REG;
		buf[3] = 0x00;
		buf[4] = 0x00;

		buf[5] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_get_data(state, 0, buf, i2c_r_data);
		pI2CData->ReadBuff[pI2CData->ReadBuffLen - 1] = i2c_r_data[1];
#endif

		
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0xC7;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

	}

 EXIT:

	
	buf[0] = USB_WRITE_I2C_CMD;
	buf[1] = 0x00;

	
	buf[5] = USB_END_I2C_CMD;
	mxl111sf_i2c_send_data(state, 0, buf);

	
	buf[2] = I2C_CONTROL_REG;
	buf[3] = 0xDF;
	buf[4] = 0x03;

	
	buf[5] = I2C_MUX_REG;
	buf[6] = 0x00;
	buf[7] = 0x00;

	
	buf[8] = USB_END_I2C_CMD;
	mxl111sf_i2c_send_data(state, 0, buf);

	
	buf[2] = I2C_MUX_REG;
	buf[3] = 0x81;
	buf[4] = 0x00;

	
	buf[5] = I2C_MUX_REG;
	buf[6] = 0x00;
	buf[7] = 0x00;

	
	buf[8] = I2C_MUX_REG;
	buf[9] = 0x00;
	buf[10] = 0x00;

	buf[11] = USB_END_I2C_CMD;
	mxl111sf_i2c_send_data(state, 0, buf);

	

	return ret;
}



int mxl111sf_i2c_xfer(struct i2c_adapter *adap,
		      struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct mxl111sf_state *state = d->priv;
	int hwi2c = (state->chip_rev > MXL111SF_V6);
	int i, ret;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		ret = (hwi2c) ?
			mxl111sf_i2c_hw_xfer_msg(state, &msg[i]) :
			mxl111sf_i2c_sw_xfer_msg(state, &msg[i]);
		if (mxl_fail(ret)) {
			mxl_printk(KERN_ERR, "failed with error %d on i2c "
				   "transaction %d of %d, %sing %d bytes "
				   "to/from 0x%02x", ret, i+1, num,
				   (msg[i].flags & I2C_M_RD) ? "read" : "writ",
				   msg[i].len, msg[i].addr);

			break;
		}
	}

	mutex_unlock(&d->i2c_mutex);

	return i == num ? num : -EREMOTEIO;
}


