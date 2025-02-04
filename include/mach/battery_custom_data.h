#ifndef _BATTERY_CUSTOM_DATA_H
#define _BATTERY_CUSTOM_DATA_H

#include <mach/mt_typedefs.h>
#include <linux/thermal_framework.h>
#include <linux/platform_data/mtk_thermal.h>
/* ============================================================ */
/* define */
/* ============================================================ */
#define BATTERY_TEMP_CRIT 62000
/* ============================================================ */
/* ENUM */
/* ============================================================ */

/* ============================================================ */
/* structure */
/* ============================================================ */
typedef struct _BATTERY_PROFILE_STRUC {
	kal_int32 percentage;
	kal_int32 voltage;
} BATTERY_PROFILE_STRUC, *BATTERY_PROFILE_STRUC_P;

typedef struct _R_PROFILE_STRUC {
	kal_int32 resistance;	/* Ohm */
	kal_int32 voltage;
} R_PROFILE_STRUC, *R_PROFILE_STRUC_P;

typedef struct _BATTERY_CYCLE_STRUC {
	kal_int32 cycle;
	kal_int32 aging_factor;
} BATTERY_CYCLE_STRUC;

typedef struct {
	kal_int32 BatteryTemp;
	kal_int32 TemperatureR;
} BATT_TEMPERATURE;

struct battery_thermal_zone {
	int num_trips;
	struct trip_t trips[THERMAL_MAX_TRIPS];
	struct work_struct therm_work;
	enum thermal_device_mode mode;
	int polling_delay;
	struct thermal_dev *therm_fw;
};

typedef struct mt_battery_meter_custom_data {
	/* ADC Channel Number */
	int cust_tabt_number;
	int vbat_channel_number;
	int isense_channel_number;
	int vcharger_channel_number;
	int vbattemp_channel_number;

	/*ADC resistor  */
	int r_bat_sense;
	int r_i_sense;
	int r_charger_1;
	int r_charger_2;

	int temperature_t0;
	int temperature_t1;
	int temperature_t1_5;
	int temperature_t2;
	int temperature_t3;
	int temperature_t;

	int fg_meter_resistance;

	int q_max_pos_50;
	int q_max_pos_25;
	int q_max_pos_10;
	int q_max_pos_0;
	int q_max_neg_10;
	bool non_linear_bat_R;

	int q_max_pos_50_h_current;
	int q_max_pos_25_h_current;
	int q_max_pos_10_h_current;
	int q_max_pos_0_h_current;
	int q_max_neg_10_h_current;

	/* Discharge Percentage */
	int oam_d5;

	int cust_tracking_point;
	int cust_r_sense;
	int cust_hw_cc;
	int aging_tuning_value;
	int cust_r_fg_offset;

	int ocv_board_compesate;	/* mV */
	int r_fg_board_base;
	int r_fg_board_slope;
	int car_tune_value;

	/* HW Fuel gague  */
	int current_detect_r_fg;
	int min_error_offset;
	int fg_vbat_average_size;
	int r_fg_value;

	int poweron_delta_capacity_tolerance;
	int poweron_low_capacity_tolerance;
	int poweron_max_vbat_tolerance;
	int poweron_delta_vbat_tolerance;

	int vbat_normal_wakeup;
	int vbat_low_power_wakeup;
	int normal_wakeup_period;
	int low_power_wakeup_period;
	int close_poweroff_wakeup_period;

	/* meter table */
	int rbat_pull_up_r;
	int rbat_pull_down_r;
	int rbat_pull_up_volt;
	int step_of_qmax;
	int power_off_threshold;

	int battery_profile_saddles;
	int battery_r_profile_saddles;
	int battery_aging_table_saddles;
	int battery_ntc_table_saddles;
	void *p_batt_temperature_table;
	void *p_battery_profile_t0;
	void *p_battery_profile_t1;
	void *p_battery_profile_t1_5;
	void *p_battery_profile_t2;
	void *p_battery_profile_t3;
	void *p_r_profile_t0;
	void *p_r_profile_t1;
	void *p_r_profile_t1_5;
	void *p_r_profile_t2;
	void *p_r_profile_t3;
	void *p_battery_profile_temperature;
	void *p_r_profile_temperature;
	void *p_battery_aging_table;
} mt_battery_meter_custom_data;

typedef struct mt_battery_charging_custom_data {
	int talking_recharge_voltage;
	int talking_sync_time;

	/* Discharge battery temperature limite */
	int max_discharge_temp;
	int min_discharge_temp;

	/* Battery Temperature Protection */
	int max_charge_temperature;
	int min_charge_temperature;
	int err_charge_temperature;
	int use_avg_temperature;

	/* Linear Charging Threshold */
	int v_pre2cc_thres;
	int v_cc2topoff_thres;
	int recharging_voltage;
	int charging_full_current;

	/* CONFIG_USB_IF */
	int usb_charger_current_suspend;
	int usb_charger_current_unconfigured;
	int usb_charger_current_configured;

	int prechg_cur;
	int usb_charger_current;
	int ac_charger_current;
	int non_std_ac_charger_current;
	int charging_host_charger_current;
	int apple_0_5a_charger_current;
	int apple_1_0a_charger_current;
	int apple_2_1a_charger_current;

	/* Charger error check */
	/* BAT_LOW_TEMP_PROTECT_ENABLE */
	int v_charger_enable;
	int v_charger_max;
	int v_charger_min;

	bool use_4_6_vindpm;

	/* Tracking time */
	int onehundred_percent_tracking_time;
	int npercent_tracking_time;
	int sync_to_real_tracking_time;

	/* JEITA parameter */
	int cust_soc_jeita_sync_time;
	int jeita_recharge_voltage;
	int jeita_temp_above_pos_60_cv_voltage;
	int jeita_temp_pos_45_to_pos_60_cv_voltage;
	int jeita_temp_pos_10_to_pos_45_cv_voltage;
	int jeita_temp_pos_0_to_pos_10_cv_voltage;
	int jeita_temp_neg_10_to_pos_0_cv_voltage;
	int jeita_temp_below_neg_10_cv_voltage;

	int jeita_temp_45_60_fast_chr_cur;
	int jeita_temp_23_45_fast_chr_cur;
	int jeita_temp_10_23_fast_chr_cur;
	int jeita_temp_0_10_fast_chr_cur;
	int jeita_temp_0_10_fast_chr_cur2;

	int temp_pos_60_threshold;
	int temp_pos_60_thres_minus_x_degree;
	int temp_pos_45_threshold;
	int temp_pos_45_thres_minus_x_degree;
	int temp_pos_10_threshold;
	int temp_pos_10_thres_plus_x_degree;
	int temp_pos_0_threshold;
	int temp_pos_0_thres_plus_x_degree;
	int temp_neg_10_threshold;
	int temp_neg_10_thres_plus_x_degree;

	/* For JEITA Linear Charging Only */
	int jeita_neg_10_to_pos_0_full_current;
	int jeita_temp_pos_45_to_pos_60_recharge_voltage;
	int jeita_temp_pos_10_to_pos_45_recharge_voltage;
	int jeita_temp_pos_0_to_pos_10_recharge_voltage;
	int jeita_temp_neg_10_to_pos_0_recharge_voltage;
	int jeita_temp_pos_45_to_pos_60_cc2topoff_threshold;
	int jeita_temp_pos_10_to_pos_45_cc2topoff_threshold;
	int jeita_temp_pos_0_to_pos_10_cc2topoff_threshold;
	int jeita_temp_neg_10_to_pos_0_cc2topoff_threshold;

	/* For charger IC GPIO config */
	int charger_enable_pin;
	int charger_otg_pin;

	/* For thermal */
	struct mtk_cooler_platform_data cool_dev;
	struct battery_thermal_zone tdata;

} mt_battery_charging_custom_data;

struct mt_battery_charging_custom_data_wrapper {
	mt_battery_charging_custom_data *data;
	struct thermal_dev_params params;
};

/* ============================================================ */
/* typedef */
/* ============================================================ */

/* ============================================================ */
/* External Variables */
/* ============================================================ */

/* ============================================================ */
/* External function */
/* ============================================================ */

#endif				/* #ifndef _BATTERY_CUSTOM_DATA_H */
