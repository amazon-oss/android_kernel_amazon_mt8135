#ifndef _EEPROM_DATA_H
#define _EEPROM_DATA_H

/* #define MT6516ISP_MaxTableSize_CNT 4096 */
/* #define EEPROM_DBG_MESSAGE */

typedef struct {
	u32 u4Offset;
	u32 u4Length;
	u8 *pu1Params;
} stEEPROM_INFO_STRUCT, *stPEEPROM_INFO_STRUCT;

/*EEPROM Data
one checksum will be a type of data in whole table, ex awb, lsc...*/
typedef struct {
	UINT16 rStartAddress;
	UINT16 rEndAddress;
	UINT16 rChecksumAddress;
} EEPROM_DATA_ADDRESS_T;
/*need to pass to the kernel to get data*/
typedef struct {
	INT32 rCheckSumPointNum;
	INT32 rDataSize;
	EEPROM_DATA_ADDRESS_T rMetaDataAddress;
	EEPROM_DATA_ADDRESS_T r2800KDataAddress;
	EEPROM_DATA_ADDRESS_T r4150KDataAddress;
	EEPROM_DATA_ADDRESS_T r5000KDataAddress;
	UINT8 rEEPROMData[EEPROM_DATA_SIZE];
} EEPROM_ALL_DATA_T;
/*need to pass to the kernel to get awb data*/
typedef struct {
	INT32 rDataSize;
	EEPROM_DATA_ADDRESS_T r2800KAWBAddress;
	EEPROM_DATA_ADDRESS_T r4150KAWBAddress;
	EEPROM_DATA_ADDRESS_T r5000KAWBAddress;
	UINT8 rEEPROMData[EEPROM_AWB_SIZE];
} EEPROM_AWB_DATA_T;
/*need to pass to the kernel to get lsc data*/
typedef struct {
	UINT8	MtkLscType;	/*LSC Table type	 "[0]sensor[1]MTK" 1*/
    UINT8	PixId;	/*0,1,2,3: B,Gb,Gr,R*/
    UINT16	TableSize;	/*table size (2108 Bytes )		2 */
    UINT32	SlimLscType;	/*00A0	FF 00 02 01 (4 bytes)		4*/
    UINT32	PreviewWH;	/*00A4	Preview Width (2 bytes)	Preview Height (2 bytes)	2*/
    UINT32	PreviewOffSet;	/*00A6	Preview Offset X (2 bytes)	Preview Offset Y (2 bytes)	2*/
    UINT32	CaptureWH;	/*00A8	Capture Width (2 bytes)	Capture Height (2 bytes)	2*/
    UINT32	CaptureOffSet;	/*00AA	Capture Offset X (2 bytes)	Capture Offfset Y (2 bytes)	2*/
    UINT32	PreviewTblSize;	/*00AC	Preview Shading Table Size (4 bytes)		4*/
    UINT32	CaptureTblSize;	/*00B0	Capture Shading Table Size (4 bytes)		4*/
    UINT32	PvIspReg[5];	/*00B4	Preview Shading Register Setting (5x4 bytes)		20*/
    UINT32	CapIspReg[5];	/*00C8	Capture Shading Register Setting (5x4 bytes)		20       */
    UINT8	CapTable[EEPROM_LSC_SIZE];	/*00DC	Capture Shading Table (16 X 16 X 2 X 4 bytes)*/
} EEPROM_LSC_MTK_TYPE;

typedef struct {
	UINT8		MtkLscType;	/*LSC Table type	"[0]sensor[1]MTK"	1*/
	UINT8		PixId;	/*0,1,2,3: B,Gb,Gr,R*/
	UINT16    TableSize;	/*TABLE SIZE		2*/
	UINT8		SensorTable[EEPROM_LSC_SIZE];	/*LSC Data (Max 1800)		1800*/
	UINT8		Reserve[EEPROM_MTK_LSC_SIZE-sizeof(UINT8)-sizeof(UINT8)-sizeof(UINT16)-(sizeof(UINT8)*EEPROM_LSC_SIZE)];
} EEPROM_LSC_SENSOR_TYPE;

typedef union {
		EEPROM_DATA_ADDRESS_T r5000KLSCAddress;
		UINT8 Data[EEPROM_MTK_LSC_SIZE];
		EEPROM_LSC_MTK_TYPE MtkLcsData;
		EEPROM_LSC_SENSOR_TYPE SensorLcsData;
} EEPROM_LSC_DATA_T;



#endif				/* _EEPROM_DATA_H */
