#ifndef _EEPROM_H
#define _EEPROM_H

#include <linux/ioctl.h>


#define EEPROMAGIC 'i'
/* IOCTRL(inode * ,file * ,cmd ,arg ) */
/* S means "set through a ptr" */
/* T means "tell by a arg value" */
/* G means "get by a ptr" */
/* Q means "get by return a value" */
/* X means "switch G and S atomically" */
/* H means "switch T and Q atomically" */

/*******************************************************************************
*
********************************************************************************/

/* EEPROM write */
#define EEPROMIOC_S_WRITE            _IOW(EEPROMAGIC, 0, stEEPROM_INFO_STRUCT)
/* EEPROM read */
#define EEPROMIOC_G_READ            _IOWR(EEPROMAGIC, 5, stPEEPROM_INFO_STRUCT)

/* EEPROM read caldata */
/*the number of checksum in the whole table of EEPROM data*/
#define EEPROM_DATA_CHECKSUM_NUM			(3)
/*the size of all EEPROM data*/
#define EEPROM_DATA_SIZE					(8192)
/*the size of AWB EEPROM data*/
#define EEPROM_AWB_SIZE						(12)
/*the size of LSC EEPROM data*/
#define EEPROM_LSC_SIZE          		  (1800)
/*the size of LSC MTK EEPROM data*/
#define EEPROM_MTK_LSC_SIZE					(1868)



#endif				/* _EEPROM_H */
