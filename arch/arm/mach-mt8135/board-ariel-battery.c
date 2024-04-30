
#include <linux/pm.h>
#include <linux/of_platform.h>
#include <asm/mach-types.h>
#include <mach/board.h>
#include <mach/battery_custom_data.h>
#include <mach/charging.h>
#include <linux/i2c.h>
#include <mt8135_ariel/cust_gpio_usage.h>
#include <mach/battery_data.h>

#include <mach/mt_gpio_def.h>
#include <mach/mt_gpio.h>
#include <mach/eint.h>

#include <mach/board-hw-cfg.h>

static struct i2c_board_info i2c_bq24297 __initdata = { I2C_BOARD_INFO("bq24297", (0xd6 >> 1)) };

#define BQ24297_BUSNUM 4

void __init mt_charger_init(void)
{
	mt_pin_set_mode(GPIOEXT22, 2);
	mt_pin_set_pull(GPIOEXT22, MT_PIN_PULL_ENABLE_UP);
	gpio_request(GPIOEXT22, "bq24297");
	gpio_direction_input(GPIOEXT22);
	i2c_bq24297.irq = EINT_IRQ(192+20);
	i2c_register_board_info(BQ24297_BUSNUM, &i2c_bq24297, 1);
}

void ariel_atl_custom_battery_init(void)
{
	pr_notice("Load ARIEL ATL table!\n");

	mt_bat_meter_data.q_max_pos_50 = 3535;
	mt_bat_meter_data.q_max_pos_50_h_current = 3502;
	mt_bat_meter_data.q_max_pos_25 = 3452;
	mt_bat_meter_data.q_max_pos_25_h_current = 3409;
	mt_bat_meter_data.q_max_pos_0 = 2819;
	mt_bat_meter_data.q_max_pos_0_h_current = 2370;
	mt_bat_meter_data.q_max_neg_10 = 2183;
	mt_bat_meter_data.q_max_neg_10_h_current = 876;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(atl_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = atl_custom_battery_aging_table;
	mt_bat_meter_data.battery_profile_saddles =
		sizeof(ariel_atl_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(ariel_atl_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);

	mt_bat_meter_data.p_battery_profile_t0 = ariel_atl_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = ariel_atl_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t2 = ariel_atl_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = ariel_atl_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = ariel_atl_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = ariel_atl_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t2 = ariel_atl_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = ariel_atl_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = ariel_atl_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = ariel_atl_custom_r_profile_temperature;
}

void ariel_sdi_custom_battery_init(void)
{
	pr_notice("Load ARIEL SDI table!\n");

	mt_bat_meter_data.q_max_pos_50 = 3528;
	mt_bat_meter_data.q_max_pos_50_h_current = 3497;
	mt_bat_meter_data.q_max_pos_25 = 3437;
	mt_bat_meter_data.q_max_pos_25_h_current = 3401;
	mt_bat_meter_data.q_max_pos_0 = 3030;
	mt_bat_meter_data.q_max_pos_0_h_current = 2750;
	mt_bat_meter_data.q_max_neg_10 = 2191;
	mt_bat_meter_data.q_max_neg_10_h_current = 1100;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(sdi_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = sdi_custom_battery_aging_table;

	mt_bat_meter_data.battery_profile_saddles =
		sizeof(ariel_sdi_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(ariel_sdi_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);
	mt_bat_meter_data.p_battery_profile_t0 = ariel_sdi_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = ariel_sdi_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t2 = ariel_sdi_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = ariel_sdi_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = ariel_sdi_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = ariel_sdi_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t2 = ariel_sdi_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = ariel_sdi_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = ariel_sdi_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = ariel_sdi_custom_r_profile_temperature;
}

void aston_atl_custom_battery_init(void)
{
	pr_notice("Load ASTON ATL table!\n");

	mt_bat_meter_data.q_max_pos_50 = 3576;
	mt_bat_meter_data.q_max_pos_50_h_current = 3526;
	mt_bat_meter_data.q_max_pos_25 = 3700;
	mt_bat_meter_data.q_max_pos_25_h_current = 3650;
	mt_bat_meter_data.q_max_pos_0 = 2564;
	mt_bat_meter_data.q_max_pos_0_h_current = 2402;
	mt_bat_meter_data.q_max_neg_10 = 2392;
	mt_bat_meter_data.q_max_neg_10_h_current = 2171;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(atl_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = atl_custom_battery_aging_table;

	mt_bat_meter_data.battery_profile_saddles =
		sizeof(aston_atl_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(aston_atl_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);

	mt_bat_meter_data.p_battery_profile_t0 = aston_atl_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = aston_atl_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t2 = aston_atl_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = aston_atl_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = aston_atl_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = aston_atl_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t2 = aston_atl_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = aston_atl_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = aston_atl_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = aston_atl_custom_r_profile_temperature;
}

void aston_sdi_custom_battery_init(void)
{
	pr_notice("Load ASTON SDI table!\n");

	mt_bat_meter_data.non_linear_bat_R = true;

	mt_bat_meter_data.q_max_pos_50 = 3532;
	mt_bat_meter_data.q_max_pos_50_h_current = 3487;
	mt_bat_meter_data.q_max_pos_25 = 3600;
	mt_bat_meter_data.q_max_pos_25_h_current = 3550;
	mt_bat_meter_data.q_max_pos_10 = 3401;
	mt_bat_meter_data.q_max_pos_10_h_current = 3148;
	mt_bat_meter_data.q_max_pos_0 = 2861;
	mt_bat_meter_data.q_max_pos_0_h_current = 2213;
	mt_bat_meter_data.q_max_neg_10 = 1565;
	mt_bat_meter_data.q_max_neg_10_h_current = 54;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(sdi_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = sdi_custom_battery_aging_table;

	mt_bat_meter_data.battery_profile_saddles =
		sizeof(aston_sdi_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(aston_sdi_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);

	mt_bat_meter_data.p_battery_profile_t0 = aston_sdi_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = aston_sdi_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t1_5 = aston_sdi_custom_battery_profile_t1_5;
	mt_bat_meter_data.p_battery_profile_t2 = aston_sdi_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = aston_sdi_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = aston_sdi_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = aston_sdi_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t1_5 = aston_sdi_custom_r_profile_t1_5;
	mt_bat_meter_data.p_r_profile_t2 = aston_sdi_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = aston_sdi_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = aston_sdi_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = aston_sdi_custom_r_profile_temperature;
}

static void thebes_battery_common_init(void)
{
	mt_bat_charging_data.jeita_temp_45_60_fast_chr_cur = 172800;
	mt_bat_charging_data.jeita_temp_23_45_fast_chr_cur = 243200;
	mt_bat_charging_data.jeita_temp_10_23_fast_chr_cur = 172800;
	mt_bat_charging_data.jeita_temp_0_10_fast_chr_cur =   64000;
	mt_bat_charging_data.jeita_temp_0_10_fast_chr_cur2 =  32000;

	mt_bat_charging_data.prechg_cur =  32000;
	mt_bat_charging_data.use_4_6_vindpm = false;

}

void thebes_atl_custom_battery_init(void)
{
	pr_notice("Load THEBES ATL table!\n");

	thebes_battery_common_init();

	mt_bat_meter_data.q_max_pos_50 = 4051;
	mt_bat_meter_data.q_max_pos_50_h_current = 3920;
	mt_bat_meter_data.q_max_pos_25 = 4012;
	mt_bat_meter_data.q_max_pos_25_h_current = 3900;
	mt_bat_meter_data.q_max_pos_0 = 3942;
	mt_bat_meter_data.q_max_pos_0_h_current = 2900;
	mt_bat_meter_data.q_max_neg_10 = 3618;
	mt_bat_meter_data.q_max_neg_10_h_current = 2097;
	mt_bat_meter_data.step_of_qmax = 54;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(thebes_atl_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = thebes_atl_custom_battery_aging_table;
	mt_bat_meter_data.battery_profile_saddles =
		sizeof(thebes_atl_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(thebes_atl_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);

	mt_bat_meter_data.p_battery_profile_t0 = thebes_atl_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = thebes_atl_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t2 = thebes_atl_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = thebes_atl_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = thebes_atl_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = thebes_atl_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t2 = thebes_atl_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = thebes_atl_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = thebes_atl_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = thebes_atl_custom_r_profile_temperature;
}

void thebes_sdi_custom_battery_init(void)
{
	pr_notice("Load THEBES SDI table!\n");

	thebes_battery_common_init();

	mt_bat_meter_data.q_max_pos_50 = 3981;
	mt_bat_meter_data.q_max_pos_50_h_current = 3830;
	mt_bat_meter_data.q_max_pos_25 = 3931;
	mt_bat_meter_data.q_max_pos_25_h_current = 3800;
	mt_bat_meter_data.q_max_pos_10 = 3713;
	mt_bat_meter_data.q_max_pos_10_h_current = 3000;
	mt_bat_meter_data.q_max_pos_0 = 3834;
	mt_bat_meter_data.q_max_pos_0_h_current = 2400;
	mt_bat_meter_data.q_max_neg_10 = 3186;
	mt_bat_meter_data.q_max_neg_10_h_current = 1350;
	mt_bat_meter_data.step_of_qmax = 54;
	mt_bat_meter_data.power_off_threshold = 3425;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(thebes_sdi_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = thebes_sdi_custom_battery_aging_table;
	mt_bat_meter_data.battery_profile_saddles =
		sizeof(thebes_sdi_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(thebes_sdi_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);

	mt_bat_meter_data.p_battery_profile_t0 = thebes_sdi_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = thebes_sdi_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t1_5 = thebes_sdi_custom_battery_profile_t1_5;
	mt_bat_meter_data.p_battery_profile_t2 = thebes_sdi_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = thebes_sdi_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = thebes_sdi_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = thebes_sdi_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t1_5 = thebes_sdi_custom_r_profile_t1_5;
	mt_bat_meter_data.p_r_profile_t2 = thebes_sdi_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = thebes_sdi_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = thebes_sdi_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = thebes_sdi_custom_r_profile_temperature;
}

static void memphis_battery_common_init(void)
{
	mt_bat_charging_data.jeita_temp_45_60_fast_chr_cur = 147200;
	mt_bat_charging_data.jeita_temp_23_45_fast_chr_cur = 204800;
	mt_bat_charging_data.jeita_temp_10_23_fast_chr_cur = 147200;
	mt_bat_charging_data.jeita_temp_0_10_fast_chr_cur =   51200;
	mt_bat_charging_data.jeita_temp_0_10_fast_chr_cur2 =  25600;

	mt_bat_charging_data.prechg_cur =  25600;
	mt_bat_charging_data.use_4_6_vindpm = false;
}

void memphis_atl_custom_battery_init(void)
{
	pr_notice("Load MEMPHIS ATL table!\n");

	memphis_battery_common_init();

	mt_bat_meter_data.q_max_pos_50 = 3259;
	mt_bat_meter_data.q_max_pos_50_h_current = 3224;
	mt_bat_meter_data.q_max_pos_25 = 3259;
	mt_bat_meter_data.q_max_pos_25_h_current = 3221;
	mt_bat_meter_data.q_max_pos_10 = 2991;
	mt_bat_meter_data.q_max_pos_10_h_current = 2953;
	mt_bat_meter_data.q_max_pos_0 = 2862;
	mt_bat_meter_data.q_max_pos_0_h_current = 2381;
	mt_bat_meter_data.q_max_neg_10 = 1547;
	mt_bat_meter_data.q_max_neg_10_h_current = 534;
	mt_bat_meter_data.step_of_qmax = 54;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(memphis_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = memphis_custom_battery_aging_table;
	mt_bat_meter_data.battery_profile_saddles =
		sizeof(memphis_atl_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(memphis_atl_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);

	mt_bat_meter_data.p_battery_profile_t0 = memphis_atl_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = memphis_atl_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t1_5 = memphis_atl_custom_battery_profile_t1_5;
	mt_bat_meter_data.p_battery_profile_t2 = memphis_atl_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = memphis_atl_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = memphis_atl_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = memphis_atl_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t1_5 = memphis_atl_custom_r_profile_t1_5;
	mt_bat_meter_data.p_r_profile_t2 = memphis_atl_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = memphis_atl_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = memphis_atl_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = memphis_atl_custom_r_profile_temperature;
}

void memphis_sdi_custom_battery_init(void)
{
	pr_notice("Load MEMPHIS SDI table!\n");

	memphis_battery_common_init();

	mt_bat_meter_data.q_max_pos_50 = 3528;
	mt_bat_meter_data.q_max_pos_50_h_current = 3497;
	mt_bat_meter_data.q_max_pos_25 = 3437;
	mt_bat_meter_data.q_max_pos_25_h_current = 3401;
	mt_bat_meter_data.q_max_pos_0 = 3030;
	mt_bat_meter_data.q_max_pos_0_h_current = 2750;
	mt_bat_meter_data.q_max_neg_10 = 2191;
	mt_bat_meter_data.q_max_neg_10_h_current = 1100;

	mt_bat_meter_data.battery_aging_table_saddles =
		sizeof(memphis_custom_battery_aging_table) / sizeof(BATTERY_CYCLE_STRUC);
	mt_bat_meter_data.p_battery_aging_table = memphis_custom_battery_aging_table;

	mt_bat_meter_data.battery_profile_saddles =
		sizeof(ariel_sdi_custom_battery_profile_temperature) / sizeof(BATTERY_PROFILE_STRUC);
	mt_bat_meter_data.battery_r_profile_saddles =
		sizeof(ariel_sdi_custom_r_profile_temperature) / sizeof(R_PROFILE_STRUC);
	mt_bat_meter_data.p_battery_profile_t0 = ariel_sdi_custom_battery_profile_t0;
	mt_bat_meter_data.p_battery_profile_t1 = ariel_sdi_custom_battery_profile_t1;
	mt_bat_meter_data.p_battery_profile_t2 = ariel_sdi_custom_battery_profile_t2;
	mt_bat_meter_data.p_battery_profile_t3 = ariel_sdi_custom_battery_profile_t3;
	mt_bat_meter_data.p_r_profile_t0 = ariel_sdi_custom_r_profile_t0;
	mt_bat_meter_data.p_r_profile_t1 = ariel_sdi_custom_r_profile_t1;
	mt_bat_meter_data.p_r_profile_t2 = ariel_sdi_custom_r_profile_t2;
	mt_bat_meter_data.p_r_profile_t3 = ariel_sdi_custom_r_profile_t3;

	mt_bat_meter_data.p_battery_profile_temperature = ariel_sdi_custom_battery_profile_temperature;
	mt_bat_meter_data.p_r_profile_temperature = ariel_sdi_custom_r_profile_temperature;
}

enum battery_module {
	ATL = 1,
	SDI = 2,
	LG = 3,
	Sony = 4,
};

enum battery_project {
	Aston = 1,
	Ariel = 2,
	Thebes = 3,
	Memphis = 4,
	Magna = 5,
	Alexandria = 6,
};

struct bat_profile {
	int project;
	int module;
	void (*custom_init) (void);
};

static struct bat_profile mt_battery_profiles[] = {
	{Ariel, ATL, ariel_atl_custom_battery_init},
	{Ariel, SDI, ariel_sdi_custom_battery_init},
	{Aston, ATL, aston_atl_custom_battery_init},
	{Aston, SDI, aston_sdi_custom_battery_init},
	{Thebes, ATL, thebes_atl_custom_battery_init},
	{Thebes, SDI, thebes_sdi_custom_battery_init},
	{Memphis, ATL, memphis_atl_custom_battery_init},
	{Memphis, SDI, memphis_sdi_custom_battery_init},
};

#ifdef CONFIG_IDME
extern unsigned int idme_get_battery_info(int, size_t);
#endif

void __init mt_custom_battery_init(void)
{
	unsigned int battery_module;
	unsigned int battery_project;
	int i;
	struct board_hw_cfg *hw_cfg;

	hw_cfg = get_hw_cfg();

#ifdef CONFIG_IDME
	battery_module = idme_get_battery_info(28, 1);
	printk(KERN_ERR "battery_module = %u\n", battery_module);
	battery_project = idme_get_battery_info(31, 2);
	printk(KERN_ERR "project = %u\n", battery_project);

	/* use Ariel/ATL if no valid IDME info */
	if (battery_module == 0)
		battery_module = ATL;
	if (battery_project == 0)
		battery_project = Ariel;
#else
	battery_module = 1;  /* default module - ATL */
	battery_project = 2; /* default project - Ariel */
#endif
	mt_bat_meter_data.non_linear_bat_R = false;
	mt_bat_meter_data.cust_tracking_point = 0;
	mt_bat_meter_data.car_tune_value = 102;
	if (hw_cfg && hw_cfg->car_tune_value)
		mt_bat_meter_data.car_tune_value = hw_cfg->car_tune_value;

	/* NTC 47K */
	mt_bat_meter_data.rbat_pull_up_r = 24000;
	mt_bat_meter_data.rbat_pull_down_r = 100000000;
	mt_bat_meter_data.cust_r_fg_offset = 0;

	mt_bat_meter_data.battery_ntc_table_saddles =
		sizeof(custom_Batt_Temperature_Table) / sizeof(BATT_TEMPERATURE);
	mt_bat_meter_data.p_batt_temperature_table = custom_Batt_Temperature_Table;
	mt_bat_charging_data.charger_enable_pin = GPIO_CHR_CE_PIN;

	mt_bat_charging_data.jeita_temp_above_pos_60_cv_voltage = BATTERY_VOLT_04_000000_V;
	mt_bat_charging_data.jeita_temp_pos_45_to_pos_60_cv_voltage = BATTERY_VOLT_04_000000_V;
	mt_bat_charging_data.jeita_temp_pos_10_to_pos_45_cv_voltage = BATTERY_VOLT_04_200000_V;
	mt_bat_charging_data.jeita_temp_pos_0_to_pos_10_cv_voltage = BATTERY_VOLT_04_000000_V;
	mt_bat_charging_data.jeita_temp_neg_10_to_pos_0_cv_voltage = BATTERY_VOLT_04_000000_V;
	mt_bat_charging_data.jeita_temp_below_neg_10_cv_voltage = BATTERY_VOLT_04_000000_V;

	mt_bat_charging_data.sync_to_real_tracking_time = 30;

	mt_bat_charging_data.use_avg_temperature = 0;
	mt_bat_charging_data.max_charge_temperature = 60;
	mt_bat_charging_data.min_charge_temperature = 0;
	mt_bat_charging_data.max_discharge_temp = 60;
	mt_bat_charging_data.min_discharge_temp = -10;
	mt_bat_charging_data.temp_pos_60_threshold = 60;
	mt_bat_charging_data.temp_pos_60_thres_minus_x_degree = 57;
	mt_bat_charging_data.temp_pos_45_threshold = 45;
	mt_bat_charging_data.temp_pos_45_thres_minus_x_degree = 44;
	mt_bat_charging_data.temp_pos_10_threshold = 15;
	mt_bat_charging_data.temp_pos_10_thres_plus_x_degree = 16;
	mt_bat_charging_data.temp_pos_0_threshold = 1;
	mt_bat_charging_data.temp_pos_0_thres_plus_x_degree = 2;
	mt_bat_charging_data.temp_neg_10_threshold = -10;
	mt_bat_charging_data.temp_neg_10_thres_plus_x_degree = -10;

	mt_bat_charging_data.jeita_temp_45_60_fast_chr_cur = 153600;
	mt_bat_charging_data.jeita_temp_23_45_fast_chr_cur = 211200;
	mt_bat_charging_data.jeita_temp_10_23_fast_chr_cur = 153600;
	mt_bat_charging_data.jeita_temp_0_10_fast_chr_cur =   51200;
	mt_bat_charging_data.jeita_temp_0_10_fast_chr_cur2 =  25600;

	mt_bat_charging_data.prechg_cur =  25600;
	mt_bat_charging_data.use_4_6_vindpm = true;		/* 4.6V for Ariel/Aston which uses MT6397 PMIC */

	/* custom settings for boards */
	for (i = 0; i < ARRAY_SIZE(mt_battery_profiles); i++) {
		if (battery_project == mt_battery_profiles[i].project &&
			battery_module == mt_battery_profiles[i].module) {
			mt_battery_profiles[i].custom_init();
			break;
		}
	}

}
