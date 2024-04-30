#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/wait.h>
#include <mach/board-hw-cfg.h>
#include <mach/mt_board.h>
#include <mach/mt_gpio_base.h>
#include <mach/mt_gpio_ext.h>

/*
The default values if one of fields is not assigned explicitely
struct board_hw_cfg {
	u16 board_id;
	u16 board_rev;
	bool has_slimport = false;
	bool has_cy_touch = false;
	bool lp855x_has_4leds_vmax25 = false;
	bool lp855x_has_4leds_vmax22_23ma = false;
	u8 lp8557_dev_ctrl = 0;
	bool has_sy_7508_touch = false;
	s16 gpio_ext_spkamp_en = 0;Invalid GPIO
};
*/

struct board_hw_cfg thebes_prehvt_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.has_sy_7508_touch = true,
	.has_icm20645 = true,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
        .is_audio_class_AB = true,
};
struct board_hw_cfg_table thebes_prehvt_table = {
	.board_id = BOARD_ID_THEBES,
	.board_rev = BOARD_REV_THEBES_PREHVT,
	.name = "Thebes preHVT",
	.cfg = &thebes_prehvt_cfg,
};


struct board_hw_cfg thebes_hvt_cfg = {
	.lp8557_has_4leds_vmax25 = true,
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.has_sy_7508_touch = true,
	.has_icm20645 = true,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_ext_spkamp_en = GPIO117 + 1, /* NOTE: here GPIO# base is 1 not 0 */
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = true,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},

	.tmp_sensor_params = {
		8100, 700, 0,
		2330, 1000, 1000,
		10060, 1000, 0
	},
	.car_tune_value = 94,

	.pmic_sensor_params = {
		1, 998, 0
	},

	.battery_sensor_params = {
		1, 999, 0
	},

	.wmt_sensor_params = {
		1, 1000, 0
	},
};

struct board_hw_cfg_table thebes_hvt_table = {
	.board_id = BOARD_ID_THEBES,
	.board_rev = BOARD_REV_THEBES_HVT,
	.name = "Thebes HVT",
	.cfg = &thebes_hvt_cfg,
};

struct board_hw_cfg thebes_evt_cfg = {
	.lp8557_has_4leds_vmax25 = true,
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.has_sy_7508_touch = true,
	.has_icm20645 = true,
	.mxt_cfg_name = "thebes",
	.use_touch_id_gpio = true,
	.touch_auto_recover = true,
	.touch_id0_gpio = GPIOEXT16,
	.touch_id1_gpio = GPIOEXT17,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = false,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},
	.has_atmel_mxt_touch = true,

	.tmp_sensor_params = {
		1100, 10, 200,
		500, 10, 200,
		10060, 1000, 0
	},

	.car_tune_value = 102,

	.pmic_sensor_params = {
		12000, 50, 200
	},

	.battery_sensor_params = {
		-3750, 10, 400
	},

	.wmt_sensor_params = {
		1, 1, 0
	},
};

struct board_hw_cfg_table thebes_evt_table = {
	.board_id = BOARD_ID_THEBES,
	.board_rev = BOARD_REV_THEBES_EVT,
	.name = "Thebes EVT",
	.cfg = &thebes_evt_cfg,
};

struct board_hw_cfg thebes_dvt_cfg = {
	.lp8557_has_4leds_vmax25 = true,
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	/* .has_sy_7508_touch = true, */
	.has_icm20645 = true,
	.mxt_cfg_name = "thebes",
	.use_touch_id_gpio = true,
	.touch_auto_recover = true,
	.touch_id0_gpio = GPIOEXT16,
	.touch_id1_gpio = GPIOEXT17,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = false,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},
	.has_atmel_mxt_touch = true,

	.tmp_sensor_params = {
		1100, 10, 200,
		500, 10, 200,
		10060, 1000, 0
	},

	.car_tune_value = 102,

	.pmic_sensor_params = {
		12000, 50, 200
	},

	.battery_sensor_params = {
		-3750, 10, 400
	},

	.wmt_sensor_params = {
		1, 1, 0
	},

	.has_no_tmp103_0x72 = true,
};

struct board_hw_cfg_table thebes_dvt_table = {
	.board_id = BOARD_ID_THEBES,
	.board_rev = BOARD_REV_THEBES_DVT,
	.name = "Thebes DVT",
	.cfg = &thebes_dvt_cfg,
};

struct board_hw_cfg_table thebes_pvt_table = {
	.board_id = BOARD_ID_THEBES,
	.board_rev = BOARD_REV_THEBES_PVT,
	.name = "Thebes PVT",
	.cfg = &thebes_dvt_cfg,		/* PVT has the same HW Cfg as DVT */
};

struct board_hw_cfg thebes_refresh_evt_cfg = {
	.lp8557_has_4leds_vmax25 = true,
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	/* .has_sy_7508_touch = true, */
	.has_icm20645 = true,
	.mxt_cfg_name = "thebes",
	.use_touch_id_gpio = true,
	.touch_auto_recover = true,
	.touch_id0_gpio = GPIOEXT16,
	.touch_id1_gpio = GPIOEXT17,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	/* .gpio_ext_spkamp_en = GPIO117 + 1 */ /* NOTE: here GPIO# base is 1 not 0 */
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = false,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},
	.has_atmel_mxt_touch = true,

	.tmp_sensor_params = {
		2000, 10, 200,
		1000, 10, 200,
		10060, 1000, 0
	},

	.car_tune_value = 102,

	.pmic_sensor_params = {
		11000, 50, 200
	},

	.battery_sensor_params = {
		-3000, 10, 400
	},

	.wmt_sensor_params = {
		1, 1, 0
	},

	.has_no_tmp103_0x72 = true,
};

struct board_hw_cfg thebes_refresh_evt_1_1_cfg = {
	.lp8557_has_4leds_vmax25 = true,
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	/* .has_sy_7508_touch = true, */
	.has_icm20645 = true,
	.mxt_cfg_name = "thebes",
	.use_touch_id_gpio = true,
	.touch_auto_recover = true,
	.touch_id0_gpio = GPIOEXT16,
	.touch_id1_gpio = GPIOEXT17,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	/* .gpio_ext_spkamp_en = GPIO117 + 1 */ /* NOTE: here GPIO# base is 1 not 0 */
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = false,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},
	.has_atmel_mxt_touch = true,

	.tmp_sensor_params = {
		2000, 10, 200,
		1000, 10, 200,
		10060, 1000, 0
	},

	.car_tune_value = 102,

	.pmic_sensor_params = {
		11000, 50, 200
	},

	.battery_sensor_params = {
		-3000, 10, 400
	},

	.wmt_sensor_params = {
		1, 1, 0
	},

	.has_no_tmp103_0x72 = true,
	.audio_flag = 0x1,
};

struct board_hw_cfg_table thebes_refresh_evt_table = {
	.board_id = BOARD_ID_THEBES,	/*NOTE: ThebesRefresh uses the same board_id as Thebes */
	.board_rev = BOARD_REV_THEBES_REFRESH_EVT,
	.name = "Thebes Refresh EVT",
	.cfg = &thebes_refresh_evt_cfg,
};

struct board_hw_cfg_table thebes_refresh_evt_1_1_table = {
	.board_id = BOARD_ID_THEBES,	/*NOTE: ThebesRefresh uses the same board_id as Thebes */
	.board_rev = BOARD_REV_THEBES_REFRESH_EVT_1_1,
	.name = "Thebes Refresh EVT 1.1",
	.cfg = &thebes_refresh_evt_1_1_cfg,
};

struct board_hw_cfg_table thebes_refresh_dvt_table = {
	.board_id = BOARD_ID_THEBES,	/*NOTE: ThebesRefresh uses the same board_id as Thebes */
	.board_rev = BOARD_REV_THEBES_REFRESH_DVT,
	.name = "Thebes Refresh DVT",
	.cfg = &thebes_refresh_evt_1_1_cfg,
};

struct board_hw_cfg_table thebes_refresh_pvt_table = {
	.board_id = BOARD_ID_THEBES,	/*NOTE: ThebesRefresh uses the same board_id as Thebes */
	.board_rev = BOARD_REV_THEBES_REFRESH_PVT,
	.name = "Thebes Refresh PVT",
	.cfg = &thebes_refresh_evt_1_1_cfg,
};

struct board_hw_cfg memphis_hvt_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.lp8557_has_4leds_vmax22_23ma = true,
	.has_sy_7508_touch = true,
	.has_atmel_mxt_touch = true,
	.mxt_cfg_name = "memphis",
	.touch_auto_recover = true,
	.has_icm20645 = true,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_ext_spkamp_en = GPIO117 + 1, /* NOTE: here GPIO# base is 1 not 0 */
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = true,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},

	.tmp_sensor_params = {
		8100, 700, 0,
		7750, 1000, 1000,
		10060, 1000, 0
	},
	.car_tune_value = 94,

	.pmic_sensor_params = {
		1, 998, 0
	},

	.battery_sensor_params = {
		1, 999, 0
	},

	.wmt_sensor_params = {
		1, 1000, 0
	},
};

struct board_hw_cfg_table memphis_hvt_table = {
	.board_id = BOARD_ID_MEMPHIS,
	.board_rev = BOARD_REV_MEMPHIS_HVT,
	.name = "Memphis HVT",
	.cfg = &memphis_hvt_cfg,
};

struct board_hw_cfg memphis_evt_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.lp8557_has_4leds_vmax22_23ma = true,
	.has_sy_7508_touch = true,
	.has_atmel_mxt_touch = true,
	.mxt_cfg_name = "memphis",
	.use_touch_id_gpio = true,
	.touch_id0_gpio = GPIOEXT16,
	.touch_id1_gpio = GPIOEXT17,
	.touch_auto_recover = true,
	.has_icm20645 = true,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = false,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},

	.tmp_sensor_params = {
		2300, 10, 200,
		750, 10, 200,
		10060, 1000, 0
	},

	.car_tune_value = 102,

	.pmic_sensor_params = {
		9750, 50, 200
	},

	.battery_sensor_params = {
		-5500, 10, 400
	},

	.wmt_sensor_params = {
		1, 1, 0
	},
};

struct board_hw_cfg_table memphis_evt_table = {
	.board_id = BOARD_ID_MEMPHIS,
	.board_rev = BOARD_REV_MEMPHIS_EVT,
	.name = "Memphis EVT",
	.cfg = &memphis_evt_cfg,
};


struct board_hw_cfg memphis_dvt_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.lp8557_has_4leds_vmax22_23ma = true,
	/* .has_sy_7508_touch = true, */
	.has_atmel_mxt_touch = true,
	.mxt_cfg_name = "memphis",
	.use_touch_id_gpio = true,
	.touch_id0_gpio = GPIOEXT16,
	.touch_id1_gpio = GPIOEXT17,
	.touch_auto_recover = true,
	.has_icm20645 = true,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.audio_spk_cfg = SPK_STEREO + 1,
	.is_audio_class_AB = false,
	.sensor_orient = {
		0, 1, 0,
		-1, 0, 0,
		0, 0, 1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},

	.tmp_sensor_params = {
		2300, 10, 200,
		750, 10, 200,
		10060, 1000, 0
	},

	.car_tune_value = 102,

	.pmic_sensor_params = {
		9750, 50, 200
	},

	.battery_sensor_params = {
		-5500, 10, 400
	},

	.wmt_sensor_params = {
		1, 1, 0
	},

	.has_no_tmp103_0x72 = true,
};

struct board_hw_cfg_table memphis_dvt_table = {
	.board_id = BOARD_ID_MEMPHIS,
	.board_rev = BOARD_REV_MEMPHIS_DVT,
	.name = "Memphis DVT",
	.cfg = &memphis_dvt_cfg,
};

struct board_hw_cfg_table memphis_pvt_table = {
	.board_id = BOARD_ID_MEMPHIS,
	.board_rev = BOARD_REV_MEMPHIS_PVT,
	.name = "Memphis PVT",
	.cfg = &memphis_dvt_cfg,	/* PVT has the same HW Cfg as DVT */
};

struct board_hw_cfg alexandria_proto_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.lp8557_has_4leds_vmax25 = true,
	.has_sy_7508_touch = true,
	.has_cy_touch = true,
	//.has_cy4x_touch = true,
	.has_icm20645 = true,
	.has_s5k5e_cam = true,
	.has_ov9724_cam = true,
	.gpio_ext_spkamp_en = GPIO117 + 1, /* NOTE: here GPIO# base is 1 not 0 */
	.gpio_wifi_combo_bfg_eint = GPIO113 + 1,
	.gpio_wifi_eint = GPIO112 + 1,
	.has_no_tmp103 = true,
	.has_at30ts74 = true,
	.audio_spk_cfg = SPK_MONO_RIGHT + 1,
        .is_audio_class_AB = false,
	.micvdd_uses_vgp1 = true,
};

struct board_hw_cfg_table alexandria_proto_table = {
	.board_id = BOARD_ID_ALEXANDRIA,
	.board_rev = BOARD_REV_ALEXANDRIA_PROTO,
	.name = "Alexandria Proto",
	.cfg = &alexandria_proto_cfg,
};

/* This is shared by ALL Aston rev (EVT, DVT and PVT) */
struct board_hw_cfg aston_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.lp8557_has_5leds = true,
	.has_slimport = true,
	.has_sy_7301_touch = true,
	.has_mpu6515 = true,
	.has_i2c_400kHz_limitation = true,
	.sensor_orient = {
		-1, 0, 0,
		0, 1, 0,
		0, 0, -1
	},
	.sec_sensor_orient = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},
	.audio_spk_cfg = SPK_STEREO + 1,
        .is_audio_class_AB = false,
	.tmp_sensor_params = {
		3400, 700, 333,
		8250, 200, 333,
		5800, 700, 334
	},

	.pmic_sensor_params = {
		1, 1, 0
	},

	.battery_sensor_params = {
		1, 1, 0
	},

	.wmt_sensor_params = {
		1, 1, 0
	},
};

struct board_hw_cfg_table aston_table = {
        .board_id = BOARD_ID_ASTON,
        .board_rev = ALL_REV,
        .name = "Aston EVT/DVT/PVT",
        .cfg = &aston_cfg,
};

struct board_hw_cfg ariel_evt1_cfg = {
	.lp8557_dev_ctrl = (LP8557_I2C_CONFIG | LP8557_DISABLE_LEDS),
	.has_slimport = true,
	.has_cy_touch = true,
	.has_mpu6515 = true,
	.has_i2c_400kHz_limitation = true,
};

struct board_hw_cfg_table ariel_evt1_table = {
	.board_id = BOARD_ID_ARIEL,
	.board_rev = BOARD_REV_ARIEL_EVT_1_0,
	.name = "Ariel EVT1",
	.cfg = &ariel_evt1_cfg,
};

struct board_hw_cfg ariel_evt1_1_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.has_slimport = true,
	.has_slimport_gpioclk = true,
	.has_cy_touch = true,
	.has_mpu6515 = true,
	.has_i2c_400kHz_limitation = true,
};

struct board_hw_cfg_table ariel_evt1_1_table = {
	.board_id = BOARD_ID_ARIEL,
	.board_rev = BOARD_REV_ARIEL_EVT_1_1,
	.name = "Ariel EVT1.1",
	.cfg = &ariel_evt1_1_cfg,
};

/* This is shared by Ariel EVT2, DVT and PVT */
struct board_hw_cfg ariel_evt2_dvt_pvt_cfg = {
	.lp8557_dev_ctrl = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.has_slimport = true,
	.has_cy_touch = true,
	.has_mpu6515 = true,
	.tmp_sensor_params = {
		8100, 700, 333,
		12300, 200, 333,
		10060, 700, 334
	},
	.pmic_sensor_params = {
		1, 1, 0
	},

	.battery_sensor_params = {
		1, 1, 0
	},

	.wmt_sensor_params = {
		1, 1, 0
	},

	.has_i2c_400kHz_limitation = true,
};

struct board_hw_cfg_table ariel_pvt_table = {
	.board_id = BOARD_ID_ARIEL,
	.board_rev = BOARD_REV_ARIEL_PVT,
	.name = "Ariel PVT",
	.cfg = &ariel_evt2_dvt_pvt_cfg,
};

struct board_hw_cfg_table ariel_dvt_table = {
	.board_id = BOARD_ID_ARIEL,
	.board_rev = BOARD_REV_ARIEL_DVT,
	.name = "Ariel DVT",
	.cfg = &ariel_evt2_dvt_pvt_cfg,
};

struct board_hw_cfg_table ariel_evt2_table = {
	.board_id = BOARD_ID_ARIEL,
	.board_rev = BOARD_REV_ARIEL_EVT_2_0,
	.name = "Ariel EVT2",
	.cfg = &ariel_evt2_dvt_pvt_cfg,
};

static struct board_hw_cfg_table *hw_confs[] = {
	&ariel_pvt_table,
	&ariel_dvt_table,
	&ariel_evt1_1_table,
	&ariel_evt1_table,
	&ariel_evt2_table,
	&aston_table,
	&thebes_prehvt_table,
	&thebes_hvt_table,
	&thebes_evt_table,
	&thebes_dvt_table,
	&thebes_pvt_table,
	&memphis_hvt_table,
	&memphis_evt_table,
	&memphis_dvt_table,
	&memphis_pvt_table,
	&alexandria_proto_table,
	&thebes_refresh_evt_table,
	&thebes_refresh_evt_1_1_table,
	&thebes_refresh_dvt_table,
	&thebes_refresh_pvt_table,
};

__init struct board_hw_cfg *get_hw_cfg(void)
{
	int i;
	u16 id, rev;
	bool board_id_match = false;
	int best_match_rev = 0;
	int best_match_index = 0;

	id = system_type;
	rev = system_rev;

	for (i = 0; i < ARRAY_SIZE(hw_confs); i++) {
		if (hw_confs[i]->board_id == id) {
			if ((hw_confs[i]->board_rev == rev) ||
				(hw_confs[i]->board_rev == ALL_REV)) {
				if (hw_confs[i]->name != NULL)
					pr_err("Found HW configs for board %s!\n",
							hw_confs[i]->name);
				return hw_confs[i]->cfg;
			}

			board_id_match = true;
			if (best_match_rev < hw_confs[i]->board_rev) {
				best_match_rev = hw_confs[i]->board_rev;
				best_match_index = i;
			}
		}
	}

	if (board_id_match) {
		pr_err("No exact match found. Use configs of latest rev=0x%x for board id=0x%x, rev=0x%x\n",
			best_match_rev, id, rev);
		return hw_confs[best_match_index]->cfg;
	}

	pr_err("board id=0x%x, rev=0x%x HW configs not found!\n", id, rev);
	return NULL;
}



