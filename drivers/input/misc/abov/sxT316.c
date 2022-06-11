/*! \file sxT316.c
 * \brief  SXT316 Driver
 *
 * Driver for the SXT316 
 * Copyright (c) 2011 Semtech Corp
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
 
//#define DEBUG
#define DRIVER_NAME "abov,sxT316"
#define SAR_FW_NAME "A96T316_fw.bin"
#define WT_ADD_SAR_HARDWARE_INFO 1  

#define MAX_WRITE_ARRAY_SIZE 32
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/input/sxT316.h>
#include <linux/firmware.h>
#if WT_ADD_SAR_HARDWARE_INFO
#include <linux/hardware_info.h>  
#endif
#include <linux/notifier.h>
#include <linux/usb.h>
#include <linux/power_supply.h>

//#include "mt_boot_common.h"
//#include "sxT316.h"
//#define CONFIG_MTK_I2C_EXTENSION 1
#define SAR_USB_CHARGED 1
#define IDLE 0
#define ACTIVE 1
#define DEBUG_LOG 0
#define APS_ERR(fmt, args...)   if(DEBUG_LOG)  {printk(KERN_ERR fmt, ##args);} 
#if SAR_SXT316_DEBUG
#define I2C_OP_MODE0_BIT		0x02
#define I2C_OP_MODE1_BIT		0x04
#define I2C_OP_MODE2_BIT		0x08
#define I2C_OP_MODE3_BIT		0x10
#define I2C_OP_MODE4_BIT		0x20
#define C_I2C_FIFO_SIZE 8
//static struct mutex lock;
struct i2c_client *g_adapter0_I2Cclient=NULL;

static u8 checksum_h;
static u8 checksum_h_bin;
static u8 checksum_l;
static u8 checksum_l_bin;

struct abov_head_info {
	u8 addr;
	u8 chanel;
	u8 speed;
	u8 op_mode;
	u8 dma_mode;
	u8 fw_get_mode;
	u8 fw_update_mode;
        u8 cmd2;
};

#endif

int wifi_status = 198;
int tel_status = 199;
psxT316XX_t abov_sar_ptr;

typedef struct sxT316
{
  pbuttonInformation_t pbuttonInformation;
	psxT316_platform_data_t hw; /* specific platform data settings */
} sxT316_t, *psxT316_t;

#if 0
static void ForcetoTouched(psxT316XX_t this)
{
  psxT316_t pDevice = NULL;
  struct input_dev *input = NULL;
  struct _buttonInfo *pCurrentButton  = NULL;

  if (this && (pDevice = this->pDevice))
  {
      pr_debug("ForcetoTouched()\n");
    
      pCurrentButton = pDevice->pbuttonInformation->buttons;
      input = pDevice->pbuttonInformation->input;

      input_report_key(input, pCurrentButton->keycode, 1);
      pCurrentButton->state = ACTIVE;

      input_sync(input);

	  pr_debug("Leaving ForcetoTouched()\n");
  }
}

#endif


static int write_register(psxT316XX_t this, u8 address, u8 value)
{
  struct i2c_client *i2c = 0;
  char buffer[2];
  int returnValue = 0;
  buffer[0] = address;
  buffer[1] = value;
  returnValue = -ENOMEM;
  if (this && this->bus) {
    i2c = this->bus;

    returnValue = i2c_master_send(i2c,buffer,2);
   APS_ERR("write_register Address: 0x%x Value: 0x%x Return: %d\n",address,value,returnValue);
  }
  if(returnValue < 0){
  	//ForcetoTouched(this);
	APS_ERR("Write_register-ForcetoTouched()\n");
  }
  return returnValue;
}


static int read_register(psxT316XX_t this, u8 address, u8 *value)
{
  struct i2c_client *i2c = 0;
  s32 returnValue = 0;
  if (this && value && this->bus) {
    i2c = this->bus;
    returnValue = i2c_smbus_read_byte_data(i2c,address);
	 printk( "read_register Address: 0x%x Return: 0x%x\n",address,returnValue);
    if (returnValue >= 0) {
      *value = returnValue;
      return 0;
    } else {
      return returnValue;
    }
  }
  
  APS_ERR("read_register-ForcetoTouched()\n");
  return -ENOMEM;
}

#if SAR_SXT316_DEBUG
static int abov_tk_reset_for_bootmode(struct i2c_client *client)
{
	int ret, retry_count = 10;
	unsigned char buf[16] = {0, };

retry:
	buf[0] = 0xF0;
	buf[1] = 0xAA;
	ret = i2c_master_send(client, buf, 2);
	if (ret < 0) {
		printk("write fail(retry:%d)\n", retry_count);
		if (retry_count-- > 0) {
			goto retry;
		}
		return -EIO;
	} else {
		printk("success reset & boot mode\n");
		return 0;
	}
}

static int abov_tk_flash_erase(struct i2c_client *client)
{
	int ret = 0;
	unsigned char buf[16] = {0, };

	buf[0] = 0xAC;
#ifdef ABOV_POWER_CONFIG
	buf[1] = 0x2D;
#else
	buf[1] = 0x2E;
#endif

	ret = i2c_master_send(client, buf, 2);
	if (ret != 2) {
		printk("SEND : fail - addr:%#x data:%#x %#x ... ret:%d\n", client->addr, buf[0], buf[1], ret);
		return -EIO;
	}

	printk("SEND : succ - addr:%#x data:%#x %#x ... ret:%d\n", client->addr, buf[0], buf[1], ret);

	return 0;
}


static int abov_tk_check_busy(struct i2c_client *client)
{
	int ret, count = 0;
	unsigned char val = 0x00;

	do {
		ret=i2c_master_recv(client,&val,1);
		if (val & 0x01) {
			count++;
			if (count > 1000){
				//i2c_ftrace("%s: val = 0x%x\r\n", __func__, val);
				return ret;
			}
		} else {
			break;
		}

	} while(1);

	return ret;
}

static int i2c_adapter_read_raw(struct i2c_client *client, u8 *data, u8 len)
{
	struct i2c_msg msg;
	int ret;
	int retry = 3;

	msg.addr = client->addr;
	msg.flags = 1;
	msg.len = len;
	msg.buf = data;
	while (retry--) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1) {
			break;
		}

		if (ret < 0) {
			printk("%s fail(data read)(%d)\n", __func__, retry);
			msleep(10);
		}
	}

	if (retry) {
		printk("TRAN : succ - addr:%#x ... len:%d \n", client->addr, msg.len);
		return 0;
	} else {
		printk("TRAN : fail - addr:%#x len:%d \n", client->addr, msg.len);
		return -EIO;
	}
}

static int abov_tk_fw_mode_enter(struct i2c_client *client)
{
	int ret = 0;
	unsigned char buf[40] = {0, };

	buf[0] = 0xAC;
	buf[1] = 0x5B;
	ret = i2c_master_send(client, buf, 2);
	if (ret != 2) {
		printk("SEND : fail - addr:%#x data:%#x %#x... ret:%d\n", client->addr, buf[0], buf[1], ret);
		return -EIO;
	}
	printk("SEND : succ - addr:%#x data:%#x %#x... ret:%d\n", client->addr, buf[0], buf[1], ret);
	msleep(5);

	ret = i2c_master_recv(client, buf, 1);
	if (ret < 0) {
		printk("Enter fw mode fail ...\n");
		return -EIO;
	}

	printk("succ ... data:%#x\n", buf[0]);

	return 0;
}

static int abov_tk_i2c_read_checksum(struct i2c_client *client)
{
	unsigned char checksum[6] = {0, };
	unsigned char buf[16] = {0, };
	int ret;

	checksum_h = 0;
	checksum_l = 0;

#ifdef ABOV_POWER_CONFIG
	buf[0] = 0xAC;
	buf[1] = 0x9E;
	buf[2] = 0x10;
	buf[3] = 0x00;
	buf[4] = 0x3F;
	buf[5] = 0xFF;
	ret = i2c_master_send(client, buf, 6);
#else
	buf[0] = 0xAC;
	buf[1] = 0x9E;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = checksum_h_bin;
	buf[5] = checksum_l_bin;
	ret = i2c_master_send(client, buf, 6);
#endif
	if (ret != 6) {
		printk("SEND : fail - addr:%#x len:%d  %d\n", client->addr, 6, ret);
		return -EIO;
	}
	mdelay(5);

	buf[0] = 0x00;
	ret = i2c_master_send(client, buf, 1);
	if (ret != 1) {
		printk("SEND : fail - addr:%#x data:%#x ... ret:%d\n", client->addr, buf[0], ret);
		return -EIO;
	}
	mdelay(5);

	ret = i2c_adapter_read_raw(client, checksum, 6);
	if (ret < 0) {
		printk("Read raw fail ... \n");
		return -EIO;
	}

	checksum_h = checksum[4];
	checksum_l = checksum[5];

	return 0;
}

static int abov_tk_fw_mode_exit(struct i2c_client *client)
{
	int ret = 0;
	unsigned char buf[40] = {0, };
	buf[0] = 0xAC;
	buf[1] = 0xE1;
	ret = i2c_master_send(client,buf,2);
	if (ret < 0 ) {
		printk("abov_tk_fw_mode_exit  err %d\n",ret);
		return -1;
	}

	return 0;
}

static int _i2c_adapter_block_write(struct i2c_client *client, u8 *data, u8 len, int outData)
{
	u8 buffer[C_I2C_FIFO_SIZE];
	u8 left = len;
	u8 offset = 0;
	u8 retry = 0;

	struct i2c_msg msg = {
		.addr = client->addr, /* & I2C_MASK_FLAG,*/
		.flags = 0,
		.buf = buffer,
	};

	if (data == NULL || len < 1) {
		printk("Invalid : data is null or len=%d\n", len);
		return -EINVAL;
	}

	while (left > 0) {
		retry = 0;
		if (left >= C_I2C_FIFO_SIZE) {
			msg.buf = &data[offset];
			msg.len = C_I2C_FIFO_SIZE;
			left -= C_I2C_FIFO_SIZE;
			offset += C_I2C_FIFO_SIZE;
		} else {
			msg.buf = &data[offset];
			msg.len = left;
			left = 0;
		}

		while (i2c_transfer(client->adapter, &msg, 1) != 1) {
			retry++;
			if (retry > 10) {
				if (outData)
					printk("OUT : fail - addr:%#x len:%d \n", client->addr, msg.len);
				else
					printk("OUT : fail - addr:%#x len:%d \n", client->addr, msg.len);
				return -EIO;
			}
		}
	}
	return 0;
}

static int i2c_adapter_block_write_nodatalog(struct i2c_client *client, u8 *data, u8 len)
{
	return _i2c_adapter_block_write(client, data, len, 0);
}

static int abov_tk_fw_write(struct i2c_client *client, unsigned char *addrH,
						unsigned char *addrL, unsigned char *val)
{
	int length = 36, ret = 0;
	unsigned char buf[40] = {0, };

	buf[0] = 0xAC;
	buf[1] = 0x7A;
	memcpy(&buf[2], addrH, 1);
	memcpy(&buf[3], addrL, 1);
	memcpy(&buf[4], val, 32);
	ret = i2c_adapter_block_write_nodatalog(client, buf, length);
	if (ret < 0) {
		printk("Firmware write fail ...\n");
		return ret;
	}

	mdelay(3);
	abov_tk_check_busy(client);

	return 0;
}

static int _abov_fw_update(struct i2c_client *client, const u8 *image, u32 size)
{
	int ret, ii = 0;
	int count;
	unsigned short address;
	unsigned char addrH, addrL;
	unsigned char data[32] = {0, };

	printk("%s: call in\r\n", __func__);

	if (abov_tk_reset_for_bootmode(client) < 0) {
		printk("sxt316 don't reset(enter boot mode)!");
		return -EIO;
	}

	msleep(45);

	for (ii = 0; ii < 10; ii++) {
		if (abov_tk_fw_mode_enter(client) < 0) {
			printk("sxt316 don't enter the download mode! %d", ii);
			msleep(40);
			continue;
		}
		break;
	}

	if (10 <= ii) {
		return -EAGAIN;
	}

	if (abov_tk_flash_erase(client) < 0) {
		printk("sxt316 don't erase flash data!");
		return -EIO;
	}

	msleep(1400);

	address = 0x800;
	count = size / 32;

	for (ii = 0; ii < count; ii++) {
		/* first 32byte is header */
		addrH = (unsigned char)((address >> 8) & 0xFF);
		addrL = (unsigned char)(address & 0xFF);
		memcpy(data, &image[ii * 32], 32);
		ret = abov_tk_fw_write(client, &addrH, &addrL, data);
		if (ret < 0) {
			printk("sxt316 fw_write.. ii = 0x%x err\r\n", ii);
			return ret;
		}

		address += 0x20;
		memset(data, 0, 32);
	}

	ret = abov_tk_i2c_read_checksum(client);
	ret = abov_tk_fw_mode_exit(client);
	if ((checksum_h == checksum_h_bin) && (checksum_l == checksum_l_bin)) {
		printk("sxt316 checksum successful. checksum_h:%x=%x && checksum_l:%x=%x\n",
			checksum_h, checksum_h_bin, checksum_l, checksum_l_bin);
	} else {
		printk("sxt316 checksum error. checksum_h:%x=%x && checksum_l:%x=%x\n",
			checksum_h, checksum_h_bin, checksum_l, checksum_l_bin);
		ret = -1;
	}
	msleep(100);

	return ret;
}

static int abov_fw_update(bool force)
{
	int update_loop;
	psxT316XX_t this = abov_sar_ptr;
	struct i2c_client *client = this->bus;
	int rc;
	bool fw_upgrade = false;
	u8 fw_version = 0, fw_file_version = 0;
	u8 fw_modelno = 0, fw_file_modeno = 0;
	const struct firmware *fw = NULL;
	char fw_name[32] = {0};

	strlcpy(fw_name, this->fw_name, NAME_MAX);

	rc = request_firmware(&fw, fw_name, this->pdev);
	if (rc < 0) {
		printk("Request firmware failed - %s (%d)\n",
				this->fw_name, rc);
		return rc;
	}

	read_register(this, ABOV_VERSION_REG, &fw_version);
	read_register(this, ABOV_MODELNO_REG, &fw_modelno);

	fw_file_modeno = fw->data[1];
	fw_file_version = fw->data[5];
	checksum_h_bin = fw->data[8];
	checksum_l_bin = fw->data[9];
	printk(KERN_INFO"sxt316 fw_version:0x%x,fw_file_version:0x%x\n",fw_version,fw_file_version);
	if ((force) || (fw_version < fw_file_version) || (fw_modelno != fw_file_modeno))
		fw_upgrade = true;
	else {
		printk("sxt316 Exiting fw upgrade...\n");
		fw_upgrade = false;
		rc = -EIO;
		goto rel_fw;
	}

	if (fw_upgrade) {
		for (update_loop = 0; update_loop < 10; update_loop++) {
			rc = _abov_fw_update(client, &fw->data[32], fw->size-32);
			if (rc < 0)
				printk("sxt316 retry : %d times!\n", update_loop);
			else {
                this->init(this);  
				break;
            }
			msleep(400);
		}
		if (update_loop >= 10)
			rc = -EIO;
	}

rel_fw:
	release_firmware(fw);
	return rc;
}

static void sar_update_work(struct work_struct *work)
{
	psxT316XX_t this = container_of(work, sxT316XX_t, fw_update_work);

	printk("%s: start update firmware\n", __func__);
	//mutex_lock(&this->mutex);
	this->loading_fw = true;
	abov_fw_update(false);
	this->loading_fw = false;
	//mutex_unlock(&this->mutex);
	printk("%s: update firmware end\n", __func__);
}

static void sar_update_work_force(struct work_struct *work)
{
	psxT316XX_t this = container_of(work, sxT316XX_t, fw_update_work);

	printk("%s: start update firmware\n", __func__);
	//mutex_lock(&this->mutex);
	this->loading_fw = true;
	abov_fw_update(true);
	this->loading_fw = false;
	//mutex_unlock(&this->mutex);
	printk("%s: update firmware end\n", __func__);
}

static int  i2c0_adapter_open(struct inode *inode, struct file *file)
{
	g_adapter0_I2Cclient->addr = 0x20 /*& I2C_MASK_FLAG*/;
	 return 0;
}
static int  i2c0_adapter_release(struct inode *inode, struct file *file)
{

	return 0;
}

static long i2c0_adapter_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	int err = 0;
	void __user *ptr = (void __user*) arg;
	u8   enable = 0;
	int  data_lsb =0;
 	int  data_msb =0;
	int  data_temp = 0;
	int  int_status = 0;
 	int  ic_vendor =0;

	switch(cmd)
	{
		case SARSENSOR_SET_MODE:
			if(copy_from_user(&enable, ptr, sizeof(enable))){
				err = -EFAULT;
				return err;	
			}

			if(enable){
				err = i2c_smbus_write_byte_data(g_adapter0_I2Cclient,SXT316_CTRL_MODE,1);
				if(err < 0)	
					return err;
			}
			else{
				err = i2c_smbus_write_byte_data(g_adapter0_I2Cclient,SXT316_CTRL_MODE,2);
				if(err < 0)
					return err;
			}

			break;

		case SARSENSOR_GET_DIFF_CH0:
			data_lsb = i2c_smbus_read_byte_data(g_adapter0_I2Cclient,SXT316_DIFF_LSB_CH0);
			data_msb = i2c_smbus_read_byte_data(g_adapter0_I2Cclient,SXT316_DIFF_MSB_CH0);
			data_temp = (data_msb << 8) | data_lsb;
			if(copy_to_user(ptr, &data_temp, sizeof(data_temp))){
				err = -EFAULT;
				return err;
			}

			break;

		case SARSENSOR_GET_DIFF_CH1:
			data_lsb = i2c_smbus_read_byte_data(g_adapter0_I2Cclient,SXT316_DIFF_LSB_CH1);
			data_msb = i2c_smbus_read_byte_data(g_adapter0_I2Cclient,SXT316_DIFF_MSB_CH1);
			data_temp = (data_msb << 8) | data_lsb;
			if(copy_to_user(ptr, &data_temp,sizeof(data_temp))){
				err = -EFAULT;
				return err;
			}

			break;

		case SARSENSOR_GET_INT_STATUS:
			int_status = i2c_smbus_read_byte_data(g_adapter0_I2Cclient,SXT316_IRQSTAT_REG);
			if(copy_to_user(ptr, &int_status, sizeof(int_status))){
				err = -EFAULT; 
				return err;
			}
	
			break;

		case SARSENSOR_GET_VENDOR_ID:
			ic_vendor = i2c_smbus_read_byte_data(g_adapter0_I2Cclient,SXT316_VENDOR_ID);
			if(copy_to_user(ptr,&ic_vendor, sizeof(ic_vendor))){
				err = -EFAULT;
				return err;
			}

			break;

		default:
			printk("%s:cmd err\n",__func__);
			err = -ENOIOCTLCMD;
			return err;
	
	}

	return 0;
}

static ssize_t i2c0_adapter_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{

	unsigned char *data = NULL;
	struct abov_head_info *info;
	int size=-1;
        int err=0;
	//mutex_lock(&lock);
 
	data = kzalloc((size_t)count, GFP_KERNEL);

	if(data==NULL){
			
		goto error;
	}

	if(copy_from_user((void *)data, (void __user *)buff, count)){              
		goto error;
	}

	info = (struct abov_head_info *)data;
	size = count-sizeof(struct abov_head_info);
	if(size == 1)
	 {
		unsigned char addr = data[sizeof(struct abov_head_info)];
		*data=i2c_smbus_read_byte_data(g_adapter0_I2Cclient,addr);
	 }
         else if (size ==4)
	 {
	    unsigned char addr1 = data[sizeof(struct abov_head_info)];
	     err=i2c_master_send(g_adapter0_I2Cclient,&addr1,1);
	      if(err< 0)
	          return -1;
	     err=i2c_master_recv(g_adapter0_I2Cclient,data,4);
              if(err< 0)
	            return -1;
		 
	 }else
	   {
	    unsigned char addr2 = data[sizeof(struct abov_head_info)];
	    *data=i2c_smbus_read_word_data(g_adapter0_I2Cclient,addr2);
           }

	if(copy_to_user((void __user *)buff, (void *)data, size)){
		goto error;
	}

error:
	//mutex_unlock(&lock);

	if(data){
		kfree(data);
	}
	return size; 

}
static ssize_t i2c0_adapter_write (struct file *filp, const char __user *buf, size_t count,loff_t *f_pos)
{

	u8 *data = NULL;
	struct abov_head_info *info;
	int size;
	int k_ret = 0;
	//mutex_lock(&lock);
	data = kzalloc((size_t)count, GFP_KERNEL);
	if(data==NULL){
		goto error;
	}
	
	if(copy_from_user((void *)data, (void __user *)buf, count)){              
		goto error;
	}

	info = (struct abov_head_info *)data;
	size = count-sizeof(struct abov_head_info); 
	
	//g_adapter0_I2Cclient->addr = info->addr & I2C_MASK_FLAG;	

	if(info->fw_update_mode){
		_abov_fw_update(g_adapter0_I2Cclient, &data[sizeof(struct abov_head_info)], size);
	}
	else if(info->op_mode&I2C_OP_MODE0_BIT){
		k_ret=i2c_smbus_write_byte_data(g_adapter0_I2Cclient,data[sizeof(struct abov_head_info)],data[sizeof(struct abov_head_info)+1]);
		if(k_ret < 0)
		  {
		    APS_ERR("sar write one byte fail\n");
		    return -1;
		  }
	}
	else if(info->op_mode&I2C_OP_MODE1_BIT){
		k_ret=i2c_smbus_write_word_data(g_adapter0_I2Cclient,data[sizeof(struct abov_head_info)],data[sizeof(struct abov_head_info)+1]);
		if(k_ret < 0)
		   {
			APS_ERR("sar write word byte fail\n");
			return -1;
	            }		  		
	}

error:
	//mutex_unlock(&lock);
	if(data){
		kfree(data);
	}
	
	return count;

}

static struct file_operations i2c0_adapter_fops = {
	.owner = THIS_MODULE,
	.open =  i2c0_adapter_open,
	.read    =  i2c0_adapter_read,  
	.write   =   i2c0_adapter_write, 
	.release =  i2c0_adapter_release,
	.unlocked_ioctl =  i2c0_adapter_unlocked_ioctl,
};

static struct miscdevice sar_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sar",
	.fops = &i2c0_adapter_fops,
};

#endif

/*! \fn static int read_regStat(psxT316XX_t this)
 * \brief Shortcut to read what caused interrupt.
 * \details This is to keep the drivers a unified
 * function that will read whatever register(s) 
 * provide information on why the interrupt was caused.
 * \param this Pointer to main parent struct 
 * \return If successful, Value of bit(s) that cause interrupt, else 0
 */
 static int read_irq_status(psxT316XX_t this)
{
	u8 data = 0;
	if (this) {
		if (read_register(this,SXT316_IRQSTAT_REG,&data) == 0) {
		    printk("sxT316 read 0x00 = 0x%x\n", (data & 0x00FF));   
            return (data & 0x00FF);
        }
	}
	return 0;
}
 static int read_channel(psxT316XX_t this)
{
	u8 data = 0;
	if (this) {
		if (read_register(this,SXT316_CH_REG,&data) == 0) {
		    printk("sxT316 read 0x08 = 0x%x\n", (data & 0x00FF));  
            return (data & 0x00FF);
        }
	}
	return 0;
}

/*! \fn static int initialize(psxT316XX_t this)
 * \brief Performs all initialization needed to configure the device
 * \param this Pointer to main parent struct 
 * \return Last used command's return value (negative if error)
 */
static int initialize(psxT316XX_t this)
{

  if (this) {
    /* prepare reset by disabling any irq handling */
    this->irq_disabled = 1;
    disable_irq(this->irq);

   write_register(this,SXT316_CTRL_MODE,2);
#ifdef CONFIG_SAR_SENSOR_CHN_SELET
    write_register(this,SXT316_CH_REG,0);
#else
   write_register(this,SXT316_CH_REG,3); 
#endif

    write_register(this,SXT316_CALIBRATION,1);
	write_register(this,0x28,0x64);
	write_register(this,0x29,0x64);
    //write_register(this,SXT316_SLEEP_THRELOD,0x0e);
    //write_register(this,SXT316_CTRL_MODE,1);

    /* re-enable interrupt handling */
    enable_irq(this->irq);
    this->irq_disabled = 0;
   
    return 0;
  }
  return -ENOMEM;
}

/*! 
 * \brief Handle what to do when a touch occurs
 * \param this Pointer to main parent struct 
 */
static void PRC_touchProcess(psxT316XX_t this)
{
  int counter = 0;
  int numberOfButtons = 0;
  psxT316_t pDevice = NULL;
  struct _buttonInfo *buttons = NULL;
  struct input_dev *input = NULL;
  struct _buttonInfo *pCurrentButton  = NULL;
   int status = 0;
   int near_far;//0--far,1--near 
  if (this && (pDevice = this->pDevice))
  {
	
    status = read_irq_status(this);
	
    buttons = pDevice->pbuttonInformation->buttons;
    input = pDevice->pbuttonInformation->input;
    numberOfButtons = pDevice->pbuttonInformation->buttonSize;
    
    if (unlikely( (buttons==NULL) || (input==NULL) )) {
      APS_ERR("ERROR!! buttons or input NULL!!!\n");
      return;
    }

  
      pCurrentButton = &buttons[0];
      if (pCurrentButton==NULL) {
        APS_ERR("ERROR!! current button at index: %d NULL!!!\n",counter);
        return; 
      }
	  near_far= (status & 0x1);
	  printk("sxT316 prc near_far %d\n",near_far);  
      switch (pCurrentButton->state) {
        case IDLE: /* Button is not being touched! */
            if(near_far){
                input_report_key(input, pCurrentButton->keycode, 1);
                input_sync(input);
                pCurrentButton->state = ACTIVE;
                // msleep(1);
                input_report_key(input, pCurrentButton->keycode, 0);
                input_sync(input);
                tel_status = pCurrentButton->keycode;
			}
            else{
                APS_ERR("LTE IDLE Button already released.\n");
            }
          	break;
        case ACTIVE: /* Button is being touched! */ 
            if(!near_far){
                input_report_key(input, pCurrentButton->keycode_release, 1);
                input_sync(input);
                pCurrentButton->state = IDLE;
                //msleep(1);
                input_report_key(input, pCurrentButton->keycode_release, 0);
                input_sync(input);
                tel_status = pCurrentButton->keycode_release;
			}
            else {
                APS_ERR("LTE Button still touched.\n");
            }
          break;
		  
        default: /* Shouldn't be here, device only allowed ACTIVE or IDLE */
          break;
      };
    }
  	
}
static void WIFI_touchProcess(psxT316XX_t this)
{
  int counter = 0;
  int numberOfButtons = 0;
  psxT316_t pDevice = NULL;
  struct _buttonInfo *buttons = NULL;
  struct input_dev *input = NULL;
  struct _buttonInfo *pCurrentButton  = NULL;
  int status = 0;
  int near_far;//0--far,1--near 
  if (this && (pDevice = this->pDevice))
  {
	
    status = read_irq_status(this);
   
    buttons = pDevice->pbuttonInformation->buttons;
    input = pDevice->pbuttonInformation->input;
    numberOfButtons = pDevice->pbuttonInformation->buttonSize;
    
    if (unlikely( (buttons==NULL) || (input==NULL) )) {
      APS_ERR("ERROR!! buttons or input NULL!!!\n");
      return;
    }
   
      pCurrentButton = &buttons[1];
      if (pCurrentButton==NULL) {
        APS_ERR("ERROR!! current button at index: %d NULL!!!\n",counter);
        return; // ERRORR!!!!
      }
	  near_far= status >> 1;
	  APS_ERR("wifi touch status %d\n",near_far);
      switch (pCurrentButton->state) {
        case IDLE: /* Button is not being touched! */
            if(near_far){
                input_report_key(input, pCurrentButton->keycode, 1);
                input_sync(input);
                pCurrentButton->state = ACTIVE;
                // msleep(1);
                input_report_key(input, pCurrentButton->keycode, 0);
                input_sync(input);
                wifi_status = pCurrentButton->keycode;
			}
            else{
                APS_ERR("WIFI IDLE Button already released.\n");
            }
          break;
        case ACTIVE: /* Button is being touched! */ 
            if(!near_far){
                input_report_key(input, pCurrentButton->keycode_release, 1);
                input_sync(input);
                pCurrentButton->state = IDLE;
                //msleep(1);
                input_report_key(input, pCurrentButton->keycode_release, 0);
                input_sync(input);
                wifi_status = pCurrentButton->keycode_release;
			}
            else {
                APS_ERR("wifi Button still touched.\n");
            }
          break;
		  
        default: /* Shouldn't be here, device only allowed ACTIVE or IDLE */
          break;
      };
    
  }	
}


static unsigned int sxT316_irq_gpio = 0;
static unsigned int irq_num;
static int of_get_sxT316_platform_data(struct device *dev)
{
	struct device_node *node = NULL;
	//int irq_num;
	int ret;

	node = of_find_compatible_node(NULL, NULL, "abov,sxT316");

	if (node) {

		irq_num = of_get_named_gpio(node, "above,irq-gpio", 0);
		if (gpio_is_valid(irq_num)) {
       		 ret = gpio_request(irq_num, "sar-sensor");

        	if (ret) {
            			printk("Could not request pwr gpio.\n");
            			return ret;
        		}
    		}

		gpio_direction_input(irq_num);
		sxT316_irq_gpio = irq_of_parse_and_map(node, 0);
		if (sxT316_irq_gpio < 0) {
			printk("sxT316 get interrupts fail!\n");	
			return -1;
		} 

	}
	return 0;
}

static int sxT316_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	strcpy(info->type, DRIVER_NAME);
	return 0;
}

/***********************ATTR START************************/
static ssize_t reg_dump_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	psxT316XX_t this = abov_sar_ptr;
	unsigned int val, reg;

	if (sscanf(buf, "%x,%x", &reg, &val) == 2) 
	{		
		printk("%s,reg = 0x%02x, val = 0x%02x\n",	__func__, *(u8 *)&reg, *(u8 *)&val);
		write_register(this, *((u8 *)&reg), *((u8 *)&val));	
	}
    return count;
}
static ssize_t reg_dump_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	
	psxT316XX_t this = abov_sar_ptr;
	u8 reg_value = 0, i;
	char *p = buf;

    for (i = 0; i < 0x2C; i++)
    {
    	read_register(this,i,&reg_value);
		p += snprintf(p, PAGE_SIZE, "(0x%02x)=0x%02x\n", i, reg_value);
    }
 
    for (i = 0x80; i < 0x84; i++)
    {
    	read_register(this,i,&reg_value);
		p += snprintf(p, PAGE_SIZE, "(0x%02x)=0x%02x\n", i, reg_value);
    }	
	
	return (p-buf);
}

static CLASS_ATTR(reg, 0660, reg_dump_show, reg_dump_store);
static int enable_sar = -1;
static ssize_t write_enable_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	psxT316XX_t this = abov_sar_ptr;
	//static u8 data_temp = 0;
	static unsigned long  val=0;
         int ret = 0;

	if (count > 2)
		return -EINVAL;
	
	ret = kstrtoul(buf, 10, &val);
	if (ret != 0)
		return ret;
	
		printk("sxt316 write_register SXT316_CH_REG val :%ld\n",val);
		ret=write_register(this,SXT316_CH_REG,val);
	if(val==0)
	{
		ret=write_register(this,SXT316_CTRL_MODE,2);
		/*if(this->irq_disabled ==0)
		{
			disable_irq(this->irq);
			this->irq_disabled=1;
		}*/
		enable_sar = 0;
		printk(KERN_ERR"sxt316 write_register  disable_irq \n");
	}
	else
	{
		/*if(this->irq_disabled ==1)
		{
			enable_irq(this->irq);
			this->irq_disabled=0;
		}*/
		ret=write_register(this,SXT316_CTRL_MODE,0);
		enable_sar =1;
		printk(KERN_ERR"sxt316 write_register enable_irq \n");
	}
	
   		 return ret;
}

static ssize_t read_enable_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	
	psxT316XX_t this = abov_sar_ptr;
	u8 reg_value = 0;

    	read_register(this,SXT316_CH_REG,&reg_value);

	return snprintf(buf, 16, "0x%x\n", reg_value);
}

static CLASS_ATTR(enable, 0664, read_enable_show, write_enable_store);

static ssize_t wifi_status_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", wifi_status);
}
static CLASS_ATTR(wifi_status, 0664, wifi_status_show, NULL);

static ssize_t tel_status_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", tel_status);
}
static CLASS_ATTR(tel_status, 0664, tel_status_show, NULL);

static ssize_t fw_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	u8 fw_version = 0;
	psxT316XX_t this = abov_sar_ptr;

	read_register(this, ABOV_VERSION_REG, &fw_version);

	return snprintf(buf, 16, "0x%x\n", fw_version);
}

static ssize_t fw_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	psxT316XX_t this = abov_sar_ptr;
	unsigned long val;
	int rc;

	if (count > 2)
		return -EINVAL;
	rc = kstrtoul(buf, 10, &val);
	if (rc != 0)
		return rc;
    
	this->irq_disabled = 1;
	disable_irq(this->irq);

	//mutex_lock(&lock);
	if (!this->loading_fw  && val) {
		this->loading_fw = true;
		abov_fw_update(true);
		this->loading_fw = false;
	}
	//mutex_unlock(&lock);
    
	enable_irq(this->irq);
	this->irq_disabled = 0;
    
	return count;
}
static CLASS_ATTR(fw, 0660, fw_show, fw_store);

static struct class sar_class = {
	.name			= "sar",
	.owner			= THIS_MODULE,
};
/***********************ATTR END************************/

/**
 * detect if abov exist or not
 * return 1 if chip exist
 * return 2 if chip exist and run bootloader
 * return 0 if chip  no exist
 */
static int abov_detect(struct i2c_client *client)
{
	s32 returnValue = 0, i;
	u8 address = SXT316_VENDOR_ID;
	u8 value = 0xAB;

	if (client) {
		for (i = 0; i < 3; i++) {
			returnValue = i2c_smbus_read_byte_data(client, address);
			printk("abov read_register for %d time Addr: 0x%x Return: 0x%x\n",
					i, address, returnValue);
			if (returnValue >= 0) {
				if (value == returnValue) {
					printk("abov detect success!\n");
					return 1;
				}
			}
			if(i == 1) {
				printk("Wait sensor ready!\n");
			}
		}
		
		for (i = 0; i < 3; i++) {
			if(abov_tk_fw_mode_enter(client) == 0)
			{
					printk("abov boot detect success!\n");
					return 2;
			}
		}

	}
	printk("abov detect failed!!!\n");
	return 0;
}
#if SAR_USB_CHARGED
static int ps_get_state(struct power_supply *psy, bool *present)
{
	union power_supply_propval pval = { 0 };
	int retval;

	retval = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT,
			&pval);
	if (retval) {
		printk("psy get property failed\n");
		return retval;
	}
	*present = (pval.intval) ? true : false;
	printk("%s is %s\n", psy->desc->name,
			(*present) ? "present" : "not present");
	return 0;
}

static int ps_notify_callback(struct notifier_block *self,
		unsigned long event, void *p)
{
	psxT316XX_t this = abov_sar_ptr;
	struct power_supply *psy = p;
	bool present;
	int retval;
        printk("-----00000\n");
        if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if(!psy)
              return NOTIFY_OK;
        if(!psy->desc)
              return NOTIFY_OK;

        if (!strcmp(psy->desc->name, "usb")){
		
		retval = ps_get_state(psy, &present);
		if (retval) {
			printk("psy get property failed\n");
			return NOTIFY_OK;
		}

		if (this->ps_is_present == present) {
			printk("ps present state not change\n");
			return NOTIFY_OK;
	}

	 this->ps_is_present = present;
	 schedule_work(&this->ps_notify_work);
	}

	return NOTIFY_OK;
}

static void ps_notify_callback_work(struct work_struct *work)
{
	psxT316XX_t this = abov_sar_ptr;
	int ret = 0;
    APS_ERR("-----11111\n");
	printk("Usb insert,going to force calibrate\n");
	ret = write_register(this,SXT316_CALIBRATION,1);
	if (ret < 0)
		printk(" Usb insert,calibrate cap sensor failed\n");
}
#endif
/*! \fn static int sxT316_probe(struct i2c_client *client, const struct i2c_device_id *id)
 * \brief Probe function
 * \param client pointer to i2c_client
 * \param id pointer to i2c_device_id
 * \return Whether probe was successful
 */
static int sxT316_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	
	psxT316XX_t this = 0;
	psxT316_t pDevice = 0;
	psxT316_platform_data_t pplatData = 0;
	struct input_dev *input = NULL;
    int i;
#if  SAR_USB_CHARGED
	struct power_supply *psy = NULL;    
#endif
#if WT_ADD_SAR_HARDWARE_INFO
    u8 fw_version = 0;
    char firmware_ver[HARDWARE_MAX_ITEM_LONGTH];
#endif
	int err = 0;
	bool isForceUpdate = false;

	printk("sxT316_probe enter!!!!\n");
    /* detect if abov exist or not */
	err = abov_detect(client);   
	
	if (err == 0) {
		return -ENODEV;
	}
	if(err == 2)
	{
		isForceUpdate = true;
	}

	// client->addr=0x20;
	of_get_sxT316_platform_data(&client->dev);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;
	this = kzalloc(sizeof(sxT316XX_t), GFP_KERNEL); /* create memory for main struct */
    abov_sar_ptr = this;

	if (this)
	{
		/* In case we need to reinitialize data 
		* (e.q. if suspend reset device) */
		this->init = initialize;
		/* shortcut to read status of interrupt */
		//this->refreshStatus = read_regStat_v1;
		#if SAR_SXT316_DEBUG
		err=misc_register(&sar_device);
		if (err < 0)
		{	
            printk("misc_register fail\n");
            return -1;
		}
		#endif
		this->irq = sxT316_irq_gpio;
		/* do we need to create an irq timer after interrupt ? */
		this->useIrqTimer = 0;
		/* Setup function to call on corresponding reg irq source bit */
		if (MAX_NUM_STATUS_BITS>= 2)
		{
	        this->statusFunc[0] = PRC_touchProcess; /* prc */
			this->statusFunc[1] = WIFI_touchProcess; /* wifi  */
		}

		/* setup i2c communication */
		this->bus = client;
		#if SAR_SXT316_DEBUG
		g_adapter0_I2Cclient = client;
		#endif
		i2c_set_clientdata(client, this);

		/* record device struct */
		this->pdev = &client->dev;

		/* create memory for device specific struct */
		this->pDevice = pDevice = kzalloc(sizeof(sxT316_t), GFP_KERNEL);
		APS_ERR("\t Initialized Device Specific Memory: 0x%p\n",pDevice);

		if (pDevice)
		{
			/* Add Pointer to main platform data struct */
			pDevice->hw = pplatData;

			/* Initialize the button information initialized with keycodes */
			pDevice->pbuttonInformation =&smtcButtonInformation;

			/* Create the input device */
			input = input_allocate_device();
			if (!input) {
				return -ENOMEM;
			}

			/* Set all the keycodes */
			__set_bit(EV_KEY, input->evbit);
			for (i = 0; i < /*pDevice->pbuttonInformation->buttonSize*/2; i++) {
				__set_bit(pDevice->pbuttonInformation->buttons[i].keycode, input->keybit);
				__set_bit(pDevice->pbuttonInformation->buttons[i].keycode_release, input->keybit);
				pDevice->pbuttonInformation->buttons[i].state = IDLE;
			}
			/* save the input pointer and finish initialization */
			pDevice->pbuttonInformation->input = input;
			input->name = "sxt316_sar";
			input->id.bustype = BUS_I2C;
			if(input_register_device(input))
				return -ENOMEM;
		}
 #if SAR_USB_CHARGED
		INIT_WORK(&this->ps_notify_work, ps_notify_callback_work);
		this->ps_notif.notifier_call = ps_notify_callback;
		err = power_supply_reg_notifier(&this->ps_notif);
		if (err) {
			printk(
				"Unable to register ps_notifier: %d\n", err);
			goto free_ps_notifier;
		}

		psy = power_supply_get_by_name("usb");
		if (psy) {
			err = ps_get_state(psy, &this->ps_is_present);
			if (err) {
				printk(
					"psy get property failed rc=%d\n",
					err);
				goto free_ps_notifier;
			}
		}
#endif
		err = class_register(&sar_class);
		if (err < 0) {
			printk("Create sysfs class failed (%d)\n", err);
			return err;
		}
        this->fw_name = SAR_FW_NAME;
		err = class_create_file(&sar_class, &class_attr_wifi_status);
		if (err < 0) {
			printk("Create wifi_status file failed (%d)\n", err);
			return err;
		}
		err = class_create_file(&sar_class, &class_attr_tel_status);
		if (err < 0) {
			printk("Create tel_status file failed (%d)\n", err);
			return err;
		}
		err = class_create_file(&sar_class, &class_attr_fw);
		if (err < 0) {
			printk("Create update_fw file failed (%d)\n", err);
			return err;
		}
		err = class_create_file(&sar_class, &class_attr_reg);
		if (err < 0) {
			printk("Create reg  file failed (%d)\n", err);
			return err;
		}
		err = class_create_file(&sar_class, &class_attr_enable);
		if (err < 0) {
			printk("Create enable  file failed (%d)\n", err);
			return err;
		}
		sxT316XX_init(this);

#if WT_ADD_SAR_HARDWARE_INFO
        read_register(this, ABOV_VERSION_REG, &fw_version);
        snprintf(firmware_ver,HARDWARE_MAX_ITEM_LONGTH,"A96T316EW,FW:0x%x",fw_version);
		hardwareinfo_set_prop(HARDWARE_SAR,firmware_ver);
#endif
        this->loading_fw = false;
		if(isForceUpdate){
			INIT_WORK(&this->fw_update_work, sar_update_work_force);
		}else{
			INIT_WORK(&this->fw_update_work, sar_update_work);
		}
		schedule_work(&this->fw_update_work);

        printk("sar probe Ok!!!!!\n\n");

		return  0;
	}
	return -1;
#if  SAR_USB_CHARGED
free_ps_notifier:
	power_supply_unreg_notifier(&this->ps_notif);

    return err;
#endif
}

/*! \fn static int sxT316_remove(struct i2c_client *client)
 * \brief Called when device is to be removed
 * \param client Pointer to i2c_client struct
 * \return Value from sxT316XX_remove()
 */
static int sxT316_remove(struct i2c_client *client)
{
	
	psxT316_t pDevice = 0;
	psxT316XX_t this = i2c_get_clientdata(client);
	if (this && (pDevice = this->pDevice))
	{
		input_unregister_device(pDevice->pbuttonInformation->input);
		
		#if SAR_SXT316_DEBUG
		misc_deregister(&sar_device);
		#endif
		
		kfree(this->pDevice);
	}
	return sxT316XX_remove(this);
}

#ifdef CONFIG_PM_SLEEP
/*====================================================*/
/***** Kernel Suspend *****/
static int sxT316_suspend(struct device *dev)
{
  struct i2c_client *client = to_i2c_client(dev);
  psxT316XX_t this = i2c_get_clientdata(client);
  static int ret =-1;
  //disable_irq(this->irq);
  
  if(enable_sar==1)
  	ret=write_register(this,SXT316_CTRL_MODE,1);
  
  printk(KERN_INFO"sar sxT316_suspend enter!!!!!,ret=%d\n",ret);
  return 0;
}
/***** Kernel Resume *****/
static int sxT316_resume(struct device *dev)
{
   struct i2c_client *client = to_i2c_client(dev);
   psxT316XX_t this = i2c_get_clientdata(client);
   static int ret =-1;
  //enable_irq(this->irq);
  
 if(enable_sar ==1)
  	ret=write_register(this,SXT316_CTRL_MODE,0);
 
 printk(KERN_INFO"sar sxT316_resume enter!!!!!ret=%d \n",ret);
  return 0;
}

static SIMPLE_DEV_PM_OPS(sxt316_pm_ops, sxT316_suspend, sxT316_resume);
#define SXT316_PM_OPS (&sxt316_pm_ops)

#else

#define SXT316_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */



#if 1
/*====================================================*/
static struct i2c_device_id sxT316_idtable[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

#endif

static const struct of_device_id sxT316_dt_ids[] = {
	{ .compatible = "abov,sxT316", },
	{ }
};

static unsigned short force[] = { 0, 0x28, I2C_CLIENT_END, I2C_CLIENT_END };
static const unsigned short *const forces[] = { force, NULL };


static struct i2c_driver sxT316_driver = {
	.probe = sxT316_probe,
	.remove = sxT316_remove,
	.detect = sxT316_i2c_detect,
	.driver.name = DRIVER_NAME,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = sxT316_dt_ids,
		   .pm = SXT316_PM_OPS,
		   },
	.id_table = sxT316_idtable,
	.address_list = (const unsigned short *)forces,
#if 0// defined(USE_KERNEL_SUSPEND)
 	.suspend  = sxT316_suspend,
  	.resume   = sxT316_resume,
#endif
};


static void sxT316XX_schedule_work(psxT316XX_t this, unsigned long delay)
{
  unsigned long flags;
  if (this) {
	 APS_ERR("sxT316XX_schedule_work()\n");
   spin_lock_irqsave(&this->lock,flags);
   /* Stop any pending penup queues */
   cancel_delayed_work(&this->dworker);
   //after waiting for a delay, this put the job in the kernel-global workqueue. so no need to create new thread in work queue.
   schedule_delayed_work(&this->dworker,delay);
   spin_unlock_irqrestore(&this->lock,flags);
  }
  else{
	
    	APS_ERR("sxT316XX_schedule_work, NULL psxT316XX_t\n");
	
      }
} 

static irqreturn_t sxT316XX_irq(int irq, void *pvoid)
{
	
	psxT316XX_t this ;

	if (pvoid) {

	this = (psxT316XX_t)pvoid;
   	sxT316XX_schedule_work(this,0);
		
   		
	}
	else
		APS_ERR("sxT316XX_irq, NULL pvoid\n");
	
	return IRQ_HANDLED;
}
static void sxT316XX_worker_func(struct work_struct *work)
{
  psxT316XX_t this = 0;
  int status =0;
  int counter = 0;
  if (work) {
    this = container_of(work,sxT316XX_t,dworker.work);
    if (!this) {
      APS_ERR("sxT316XX_worker_func, NULL sxT316XX_t\n");
      return;
    }
	status = read_channel(this);
	APS_ERR("sar work fun status %d\n",status);
	counter = -1;
	while((++counter) < 1){
	 
	 switch(status){
		case 1:
			 this->statusFunc[0](this);
			 break;
		case 2:
			 this->statusFunc[1](this);
		         break;
		case 3:
			 this->statusFunc[0](this);
			 this->statusFunc[1](this);
			  break;
		default:
			break;
	 	}	
     }

   if (unlikely(this->useIrqTimer ))
    { /* Early models and if RATE=0 for newer models require a penup timer */
      /* Queue up the function again for checking on penup */
		sxT316XX_schedule_work(this,msecs_to_jiffies(this->irqTimeout));
    }
  } else {
    APS_ERR("sxT316XX_worker_func, NULL work_struct\n");
  }
}



void sxT316XX_suspend(psxT316XX_t this)
{
  if (this)
    disable_irq(this->irq);
}
void sxT316XX_resume(psxT316XX_t this)
{
  if (this) {

  sxT316XX_schedule_work(this,0);
    enable_irq(this->irq);
  }
}


int sxT316XX_init(psxT316XX_t this)
{
  int err = 0;
  if (this && this->pDevice)
  {

    /* initialize spin lock */
  	spin_lock_init(&this->lock);
	INIT_DELAYED_WORK(&this->dworker, sxT316XX_worker_func);
    /* initailize interrupt reporting */
    this->irq_disabled = 0;
    err = request_irq(this->irq, sxT316XX_irq, IRQF_TRIGGER_FALLING,"sxT316", this);
	  if (err) {
		  printk("irq %d busy?\n", this->irq);
		  return err;
		  }
    if (this->init)
         return this->init(this);
	
    printk("No init function!!!!\n");
	}
	return -ENOMEM;
}

int sxT316XX_remove(psxT316XX_t this)
{
  if (this) {
    cancel_delayed_work_sync(&this->dworker);

  if(this->irq)
   {
    free_irq(this->irq, this);
    gpio_free(irq_num);
  }

    kfree(this);
    return 0;
  }
  return -ENOMEM;
}

static int __init sxT316_init(void)
{
	  printk("sxT316_init !!!!\n");
       return i2c_add_driver(&sxT316_driver);
}
static void __exit sxT316_exit(void)
{
	
	i2c_del_driver(&sxT316_driver);
}

late_initcall(sxT316_init);
module_exit(sxT316_exit);

MODULE_AUTHOR("Semtech Corp. (http://www.semtech.com/)");
MODULE_DESCRIPTION("mediatek,sxT316");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

