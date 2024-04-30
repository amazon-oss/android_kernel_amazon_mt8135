/*************************************************************************************************
s5k5e2ya_otp.c
---------------------------------------------------------
OTP Application file From Truly for s5k5e2ya
2015.01.13
---------------------------------------------------------
NOTE:
The modification is appended to initialization of image sensor.
After sensor initialization, use the function , and get the id value.

**************************************************************************************************/


#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/slab.h>

#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "eeprom.h"
#include "eeprom_define.h"

#include "s5k5e2yamipiraw_Sensor.h"

#include <linux/xlog.h>
/****************************Modify following Strings for debug****************************/
#define PFX "s5k5e2ya_otp"
#define MYLOGERR(format, args...)    pr_err("[%s] " format, __func__, ##args)
#define MYLOG(format, args...)    pr_debug("[%s] " format, __func__, ##args)
#define LOG_INF(format, args...)  pr_debug("[%s] " format, __func__, ##args)
/****************************Modify following Strings for debug****************************/


/* extern void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para); */
/* extern void S5K5E2YA_wordwrite_cmos_sensor(kal_uint32 addr, kal_uint32 para); */

#define USHORT             unsigned short
#define BYTE               unsigned char
#define Sleep(ms) mdelay(ms)

/*USHORT golden_r;
USHORT golden_gr;
USHORT golden_gb;
USHORT golden_b;

USHORT current_r;
USHORT current_gr;
USHORT current_gb;
USHORT current_b;

kal_uint32 r_ratio;
kal_uint32 b_ratio;*/

/*************************************************************************************************
* Function    :  S5K5E2YA_check_eeprom_data
* Description :  get otp model value: 1->Mud, 2->Doom
* Parameters  :
**************************************************************************************************/
bool S5K5E2YA_check_eeprom_start_data(void)
{
	kal_uint8 model;
	kal_uint8 year;
	MYLOG("S5K5E2YA EEPROM check start:\n");
	model = S5K5E2YA_read_eeprom_sensor(0x0020);
	year = S5K5E2YA_read_eeprom_sensor(0x002a);
	 if (model != 0x02 || year != 0x0e) {
		MYLOG("S5K5E2YA EEPROM data not matched: model = 0x%02x, year = 0x%02x\n", model,
		       year);
		return 0;
	}
	 return 1;
}


/*************************************************************************************************
* Function    :  S5K5E2YA_get_eeprom_metadata
* Description :  Get otp data: Image Sensor ID, Model, Build Revision, CM Vendor, Substrate,
			    EEPROM Manufacture, IR Glass, Lens, Flex, week, year, serial Number,
			    EEPROM Revision, System Info Checksum
* Parameters  :
**************************************************************************************************/
int S5K5E2YA_get_eeprom_metadata(EEPROM_ALL_DATA_T *data)
{
	int i;
	int sum = 0;
	int checksum = 0;
	 MYLOG("S5K5E2YA get EEPROM meta data:\n");
	 for (i = data->rMetaDataAddress.rStartAddress; i <= data->rMetaDataAddress.rEndAddress;
		i++) {
		data->rEEPROMData[i] = S5K5E2YA_read_eeprom_sensor((kal_uint16) i);
		sum += data->rEEPROMData[i];

		    /* MYLOG("Address: 0x%02x ---- S5K5E2YA cal data : 0x%02x\n", i, data[i]); */
		}

	    /* get metadata checksum */
	    sum = 0x00FF - (sum & 0x00FF);
	MYLOG("S5K5E2YA meta checksum: i: 0x%04x, struct: 0x%04x\n", i,
	       data->rMetaDataAddress.rChecksumAddress);
	checksum =
	    S5K5E2YA_read_eeprom_sensor((kal_uint16) data->rMetaDataAddress.rChecksumAddress);
	data->rEEPROMData[i] = checksum;
	 MYLOG("S5K5E2YA EEPROM data sum = 0x%02x, checksum = 0x%02x\n", sum, checksum);
	if (checksum != sum) {
		MYLOG("S5K5E2YA meta data checksum error!!\n");
		return -1;
	} else {
		MYLOG("S5K5E2YA meta data checksum success!!\n");
	}
	return 0;
}


/*************************************************************************************************
* Function    :  S5K5E2YA_get_eeprom_cal_data
* Description :  Get LSC data
* Parameters  :  [BYTE] zone : OTP PAGE index , 0x00~0x0f
**************************************************************************************************/
int S5K5E2YA_get_cal_data(EEPROM_ALL_DATA_T *data)
{
	int i;
	int j;
	int sum = 0;
	int checksum = 0;

	    /* kal_uint8 testdata; */

	    /*MYLOG("S5K5E2YA get EEPROM cal data:\n");
	       MYLOG("S5K5E2YA data size: %d\n", data->pDataSize);
	       MYLOG("S5K5E2YA checksum size: %d\n", data->pCheckSumPointNum); */
	    for (i = 0; i < data->rDataSize; i++) {
		data->rEEPROMData[i] = S5K5E2YA_read_eeprom_sensor((kal_uint16) i);

		    /* MYLOG("Address: 0x%02x ---- S5K5E2YA cal data : 0x%02x\n", i, data->pEEPROMData[i]); */
		}

	    /* MYLOG("metadata checksum address: start: 0x%04x end: 0x%04x\n", data->pMetaDataAddress.pStartAddress,
		data->pMetaDataAddress.pEndAddress); */
	    /* check metadata checksum */
	    for (j = (data->rMetaDataAddress.rStartAddress);
		 j <= (data->rMetaDataAddress.rEndAddress); j++) {
		sum += data->rEEPROMData[j];
		}
	checksum = data->rEEPROMData[data->rMetaDataAddress.rChecksumAddress];
	sum = 0x00FF - (sum & 0x00FF);

	    /* MYLOG("S5K5E2YA meta checksum address: 0x%04x\n", data->pMetaDataAddress.pChecksumAddress); */
	    /* MYLOG("S5K5E2YA meta data sum = 0x%04x, checksum = 0x%04x\n", sum , checksum); */
	    if (checksum != sum) {
		MYLOGERR("S5K5E2YA meta data checksum error!!\n");
		MYLOGERR("S5K5E2YA meta data sum = 0x%04x, checksum = 0x%04x\n", sum, checksum);
		return -1;
	} else {
		MYLOG("S5K5E2YA meta checksum success!!\n");
	}

	    /* check 2800K checksum */
	    sum = 0;
	checksum = 0;
	for (j = data->r2800KDataAddress.rStartAddress; j <= data->r2800KDataAddress.rEndAddress;
	      j++) {
		sum += data->rEEPROMData[j];
		}
	checksum = data->rEEPROMData[data->r2800KDataAddress.rChecksumAddress];
	sum = 0x00FF - (sum & 0x00FF);

	    /* MYLOG("S5K5E2YA 2800K data sum = 0x%04x, checksum = 0x%04x\n", sum , checksum); */
	    if (checksum != sum) {
		MYLOGERR("S5K5E2YA 2800K data checksum error!!\n");
		MYLOGERR("S5K5E2YA 2800K data sum = 0x%04x, checksum = 0x%04x\n", sum, checksum);
		return -1;
	} else {
		MYLOG("S5K5E2YA 2800K checksum success!!\n");
	}

	    /* check 4150K checksum */
	    sum = 0;
	checksum = 0;
	for (j = data->r4150KDataAddress.rStartAddress; j <= data->r4150KDataAddress.rEndAddress;
	      j++) {
		sum += data->rEEPROMData[j];
		}
	checksum = data->rEEPROMData[data->r4150KDataAddress.rChecksumAddress];
	sum = 0x00FF - (sum & 0x00FF);

	    /* MYLOG("S5K5E2YA 4150K data sum = 0x%04x, checksum = 0x%04x\n", sum , checksum); */
	    if (checksum != sum) {
		MYLOGERR("S5K5E2YA 4150K data checksum error!!\n");
		MYLOGERR("S5K5E2YA 4150K data sum = 0x%04x, checksum = 0x%04x\n", sum, checksum);
		return -1;
	} else {
		MYLOG("S5K5E2YA 4150K checksum success!!\n");
	}

	    /* check 5000K checksum */
	    sum = 0;
	checksum = 0;
	for (j = data->r5000KDataAddress.rStartAddress; j <= data->r5000KDataAddress.rEndAddress;
	      j++) {
		sum += data->rEEPROMData[j];
		}
	checksum = data->rEEPROMData[data->r5000KDataAddress.rChecksumAddress];
	sum = 0x00FF - (sum & 0x00FF);

	    /* MYLOG("S5K5E2YA 5000K data sum = 0x%04x, checksum = 0x%04x\n", sum , checksum); */
	    if (checksum != sum) {
		MYLOGERR("S5K5E2YA 5000K data checksum error!!\n");
		MYLOGERR("S5K5E2YA 5000K data sum = 0x%04x, checksum = 0x%04x\n", sum, checksum);
		return -1;
	} else {
		MYLOG("S5K5E2YA 5000K checksum success!!\n");
	}
	 return i;
}

/*************************************************************************************************
* Function    :  S5K5E2YA_get_eeprom_awb_data
* Description :  Get awb eeprom data
* Parameters  :
**************************************************************************************************/
int S5K5E2YA_get_eeprom_awb_data(EEPROM_AWB_DATA_T *data)
{

	int i = 0;
	int count = 0;
	MYLOG("S5K5E2YA_get_eeprom_awb_data+\n");

	MYLOG("S5K5E2YA 2800K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
	       data->r2800KAWBAddress.rStartAddress, data->r2800KAWBAddress.rEndAddress,
	       data->r2800KAWBAddress.rChecksumAddress);
	MYLOG("S5K5E2YA 4150K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
	       data->r4150KAWBAddress.rStartAddress, data->r4150KAWBAddress.rEndAddress,
	       data->r4150KAWBAddress.rChecksumAddress);
	MYLOG("S5K5E2YA 5000K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
	       data->r5000KAWBAddress.rStartAddress, data->r5000KAWBAddress.rEndAddress,
	       data->r5000KAWBAddress.rChecksumAddress);
	/*2088KAWB*/
	for (i = data->r2800KAWBAddress.rStartAddress; i <= data->r2800KAWBAddress.rEndAddress; i++) {
		data->rEEPROMData[count] = S5K5E2YA_read_eeprom_sensor((kal_uint16) i);
		count++;
	}
	/*4150KAWB*/
	for (i = data->r4150KAWBAddress.rStartAddress; i <= data->r4150KAWBAddress.rEndAddress; i++) {
		data->rEEPROMData[count] = S5K5E2YA_read_eeprom_sensor((kal_uint16) i);
		count++;
	}
	/*5000KAWB*/
	for (i = data->r5000KAWBAddress.rStartAddress; i <= data->r5000KAWBAddress.rEndAddress; i++) {
		data->rEEPROMData[count] = S5K5E2YA_read_eeprom_sensor((kal_uint16) i);
		count++;
	}

	MYLOG("S5K5E2YA_get_eeprom_awb_data-\n");
	return 0;
}


/*************************************************************************************************
* Function    :  S5K5E2YA_get_eeprom_lsc_data
* Description :  Get lsc eeprom data
* Parameters  :
**************************************************************************************************/
int S5K5E2YA_get_eeprom_lsc_data(EEPROM_LSC_DATA_T *lsc_data, int *flag)
{

	int i = 0;
	int count = 0;

	MYLOG("S5K5E2YA_get_eeprom_lsc_data+\n");

	MYLOG("S5K5E2YA flag %d\n", *flag);
	if (*flag == 0) {
		MYLOG("S5K5E2YA 5000K lsc eeprom data: first time open, read data from eeprom.\n");
		lsc_data->r5000KLSCAddress.rStartAddress = 0x16C8;
		lsc_data->r5000KLSCAddress.rEndAddress = 0x1E13;
		lsc_data->r5000KLSCAddress.rChecksumAddress = 0x1E16;
		MYLOG("S5K5E2YA 5000K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
		       lsc_data->r5000KLSCAddress.rStartAddress, lsc_data->r5000KLSCAddress.rEndAddress,
		       lsc_data->r5000KLSCAddress.rChecksumAddress);

		/*5000KLSC*/
		for (i = 0x16C8; i <= 0x1E13; i++) {
			lsc_data->Data[count] = S5K5E2YA_read_eeprom_sensor((kal_uint16) i);
			count++;
		}
		MYLOG("S5K5E2YA lsc eeprom: Data[0]: 0x%04x\n", lsc_data->Data[0]);
		MYLOG("S5K5E2YA lsc eeprom: Data[1]: 0x%04x\n", lsc_data->Data[1]);
		MYLOG("S5K5E2YA lsc eeprom: Data[2]: 0x%04x\n", lsc_data->Data[2]);
		MYLOG("S5K5E2YA lsc eeprom: Data[3]: 0x%04x\n", lsc_data->Data[3]);
		MYLOG("S5K5E2YA lsc eeprom: Data[%d]: 0x%04x\n", (count-1), lsc_data->Data[count-1]);
	} else {

		MYLOG("S5K5E2YA 5000K lsc eeprom data: only copy data.\n");
		for (count = 0; count < EEPROM_LSC_SIZE; count++)
			lsc_data->MtkLcsData.CapTable[count] = S5K5E2YA_lsc_eeprom_data.Data[68+count];

		lsc_data->MtkLcsData.MtkLscType = 0x02;
		lsc_data->MtkLcsData.PixId = 0x0;
		lsc_data->MtkLcsData.TableSize = EEPROM_MTK_LSC_SIZE;
		lsc_data->MtkLcsData.SlimLscType = (S5K5E2YA_lsc_eeprom_data.Data[3]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[2]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[1]<<8)+S5K5E2YA_lsc_eeprom_data.Data[0];
		lsc_data->MtkLcsData.PreviewWH = (S5K5E2YA_lsc_eeprom_data.Data[7]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[6]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[5]<<8)+S5K5E2YA_lsc_eeprom_data.Data[4];
		lsc_data->MtkLcsData.PreviewOffSet = (S5K5E2YA_lsc_eeprom_data.Data[11]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[10]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[9]<<8)+S5K5E2YA_lsc_eeprom_data.Data[8];
		lsc_data->MtkLcsData.CaptureWH = (S5K5E2YA_lsc_eeprom_data.Data[15]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[14]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[13]<<8)+S5K5E2YA_lsc_eeprom_data.Data[12];
		lsc_data->MtkLcsData.CaptureOffSet = (S5K5E2YA_lsc_eeprom_data.Data[19]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[18]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[17]<<8)+S5K5E2YA_lsc_eeprom_data.Data[16];
		lsc_data->MtkLcsData.PreviewTblSize = (S5K5E2YA_lsc_eeprom_data.Data[23]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[22]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[21]<<8)+S5K5E2YA_lsc_eeprom_data.Data[20];
		lsc_data->MtkLcsData.CaptureTblSize = (S5K5E2YA_lsc_eeprom_data.Data[27]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[26]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[25]<<8)+S5K5E2YA_lsc_eeprom_data.Data[24];

		for (count = 0; count < 5; count++) {
			lsc_data->MtkLcsData.PvIspReg[count] = (S5K5E2YA_lsc_eeprom_data.Data[31+4*count]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[30+4*count]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[29+4*count]<<8)+S5K5E2YA_lsc_eeprom_data.Data[28+4*count];
			lsc_data->MtkLcsData.CapIspReg[count] = (S5K5E2YA_lsc_eeprom_data.Data[51+4*count]<<24)+(S5K5E2YA_lsc_eeprom_data.Data[50+4*count]<<16)+(S5K5E2YA_lsc_eeprom_data.Data[49+4*count]<<8)+S5K5E2YA_lsc_eeprom_data.Data[48+4*count];
		}
	}

	MYLOG("S5K5E2YA_get_eeprom_lsc_data-\n");
	return 0;
}

/*************************************************************************************************
* Function    :  S5K5E2YA_get_eeprom_full_data
* Description :  Get full eeprom user data, and check the checksum is correct for all data and LSC
* Parameters  :
**************************************************************************************************/
int S5K5E2YA_get_eeprom_full_data(EEPROM_ALL_DATA_T *fulldata)
{
	int checkreturn = 0;

	    /* int i, j; */
	    /* int sum = 0; */
	    /* int checksum = 0; */
	    MYLOG("S5K5E2YA_get_eeprom_full_data+\n");

	    /*checkreturn = S5K5E2YA_get_eeprom_metadata( fulldata );
	       if( checkreturn == -1)
	       return -1; */
	    MYLOG("S5K5E2YA metadata flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
		  fulldata->rMetaDataAddress.rStartAddress, fulldata->rMetaDataAddress.rEndAddress,
		  fulldata->rMetaDataAddress.rChecksumAddress);
	MYLOG("S5K5E2YA 2800K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
	       fulldata->r2800KDataAddress.rStartAddress, fulldata->r2800KDataAddress.rEndAddress,
	       fulldata->r2800KDataAddress.rChecksumAddress);
	MYLOG("S5K5E2YA 4150K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
	       fulldata->r4150KDataAddress.rStartAddress, fulldata->r4150KDataAddress.rEndAddress,
	       fulldata->r4150KDataAddress.rChecksumAddress);
	MYLOG("S5K5E2YA 5000K flag: start: 0x%04x, end: 0x%04x, checksum: 0x%04x\n",
	       fulldata->r5000KDataAddress.rStartAddress, fulldata->r5000KDataAddress.rEndAddress,
	       fulldata->r5000KDataAddress.rChecksumAddress);
	checkreturn = S5K5E2YA_get_cal_data(fulldata);
	if (checkreturn == -1)
		return -1;

	    /* MYLOG("S5K5E2YA startflag: %d\n", startflag); */
	    /* MYLOG("S5K5E2YA EEPROM meta data: Mode: 0x%02x, Year: 0x%02x\n", fulldata[32], fulldata[42] ); */
	    MYLOG("S5K5E2YA_get_eeprom_full_data-\n");
	 return 0;
}
