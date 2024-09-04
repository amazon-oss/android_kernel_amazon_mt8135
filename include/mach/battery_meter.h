#ifndef _BATTERY_METER_H
#define _BATTERY_METER_H

#include <mach/mt_typedefs.h>
#include <mach/battery_custom_data.h>
/* ============================================================ */
/* define */
/* ============================================================ */

/* ============================================================ */
/* ENUM */
/* ============================================================ */

/* ============================================================ */
/* structure */
/* ============================================================ */

/* ============================================================ */
/* typedef */
/* ============================================================ */

/* ============================================================ */
/* External Variables */
/* ============================================================ */
extern int g_R_BAT_SENSE;
extern int g_R_I_SENSE;
extern int g_R_CHARGER_1;
extern int g_R_CHARGER_2;

#if defined(CONFIG_AMAZON_METRICS_LOG)
extern kal_int32 gFG_BATT_CAPACITY_aging;
extern kal_int32 gFG_BATT_CAPACITY;
#endif

/* ============================================================ */
/* External function */
/* ============================================================ */
extern kal_int32 battery_meter_get_battery_voltage(void);
extern kal_int32 battery_meter_get_charging_current(void);
extern kal_int32 battery_meter_get_battery_current(void);
extern kal_bool battery_meter_get_battery_current_sign(void);
extern kal_int32 battery_meter_get_car(void);
extern kal_int32 battery_meter_get_battery_temperature(void);
extern kal_int32 battery_meter_get_charger_voltage(void);
extern kal_int32 battery_meter_get_battery_percentage(void);
extern kal_int32 battery_meter_initial(void);
extern kal_int32 battery_meter_reset(kal_bool bUI_SOC);
extern kal_int32 battery_meter_sync(kal_int32 bat_i_sense_offset);

extern kal_int32 battery_meter_get_battery_zcv(void);
extern kal_int32 battery_meter_get_battery_nPercent_zcv(void);	/* 15% zcv,  15% can be customized */
extern kal_int32 battery_meter_get_battery_nPercent_UI_SOC(void);	/* tracking point */

extern kal_int32 battery_meter_get_tempR(kal_int32 dwVolt);
extern kal_int32 battery_meter_get_tempV(void);
extern kal_int32 battery_meter_get_VSense(void);	/* isense voltage */

extern int get_rtc_spare_fg_value(void);
extern unsigned long rtc_read_hw_time(void);
extern kal_int32 battery_meter_get_battery_voltage_cached(void);

extern kal_int32 battery_meter_get_battery_soc(void);
#ifdef CONFIG_IDME
extern unsigned int idme_get_battery_info(int, size_t);
#endif

#ifdef CONFIG_MTK_BATTERY_CVR_SUPPORT
#define ATL_BATTERY_3900_PROFILE_NUM 51
#define ATL_BATTERY_3950_PROFILE_NUM 57
#define ATL_BATTERY_4000_PROFILE_NUM 62
#define ATL_BATTERY_4096_PROFILE_NUM 69
#define ATL_BATTERY_4200_PROFILE_NUM 68
#define ATL_BATTERY_MAX_PROFILE_NUM  69

#define SDI_BATTERY_3900_PROFILE_NUM 51
#define SDI_BATTERY_3950_PROFILE_NUM 58
#define SDI_BATTERY_4000_PROFILE_NUM 62
#define SDI_BATTERY_4096_PROFILE_NUM 70
#define SDI_BATTERY_4200_PROFILE_NUM 67
#define SDI_BATTERY_MAX_PROFILE_NUM  70

extern BATTERY_PROFILE_STRUC *aston_atl_custom_battery_profile_array[];
extern R_PROFILE_STRUC *aston_atl_custom_r_profile_array[];

extern BATTERY_PROFILE_STRUC *aston_sdi_custom_battery_profile_array[];
extern R_PROFILE_STRUC *aston_sdi_custom_r_profile_array[];

extern BATTERY_PROFILE_STRUC aston_atl_custom_battery_profile_t0[];
extern R_PROFILE_STRUC aston_atl_custom_r_profile_t0[];
extern BATTERY_PROFILE_STRUC aston_atl_custom_battery_profile_t1[];
extern R_PROFILE_STRUC aston_atl_custom_r_profile_t1[];
extern BATTERY_PROFILE_STRUC aston_atl_custom_battery_profile_t2[];
extern R_PROFILE_STRUC aston_atl_custom_r_profile_t2[];
extern BATTERY_PROFILE_STRUC aston_atl_custom_battery_profile_t3[];
extern R_PROFILE_STRUC aston_atl_custom_r_profile_t3[];

extern BATTERY_PROFILE_STRUC aston_sdi_custom_battery_profile_t0[];
extern R_PROFILE_STRUC aston_sdi_custom_r_profile_t0[];
extern BATTERY_PROFILE_STRUC aston_sdi_custom_battery_profile_t1[];
extern R_PROFILE_STRUC aston_sdi_custom_r_profile_t1[];
extern BATTERY_PROFILE_STRUC aston_sdi_custom_battery_profile_t1_5[];
extern R_PROFILE_STRUC aston_sdi_custom_r_profile_t1_5[];
extern BATTERY_PROFILE_STRUC aston_sdi_custom_battery_profile_t2[];
extern R_PROFILE_STRUC aston_sdi_custom_r_profile_t2[];
extern BATTERY_PROFILE_STRUC aston_sdi_custom_battery_profile_t3[];
extern R_PROFILE_STRUC aston_sdi_custom_r_profile_t3[];

extern BATTERY_PROFILE_STRUC aston_atl_custom_battery_profile_temperature[];
extern R_PROFILE_STRUC aston_atl_custom_r_profile_temperature[];
extern BATTERY_PROFILE_STRUC aston_sdi_custom_battery_profile_temperature[];
extern R_PROFILE_STRUC aston_sdi_custom_r_profile_temperature[];
#endif

#endif				/* #ifndef _BATTERY_METER_H */
