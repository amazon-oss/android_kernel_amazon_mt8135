#include <linux/kernel.h>
#include <linux/lp855x.h>

#define SPK_STEREO 0
#define SPK_MONO_LEFT 1
#define SPK_MONO_RIGHT 2

struct board_hw_cfg {
	bool has_s5k5e_cam;
	bool has_ov9724_cam;
	bool has_slimport;
	bool has_slimport_gpioclk;
	bool has_cy_touch;
	bool has_icm20645;
	bool has_mpu6515;
	s8 sensor_orient[9];
	s8 sec_sensor_orient[9];
	bool lp8557_has_5leds;
	bool lp8557_has_4leds_vmax25;
	bool lp8557_has_4leds_vmax22_23ma;
	u8 lp8557_dev_ctrl;
	bool has_sy_7508_touch;
	bool has_sy_7301_touch;
	bool touch_auto_recover;
	bool has_no_tmp103;
	bool has_at30ts74; /* Atmel temp sensor */
	bool has_atmel_mxt_touch;
	bool use_touch_id_gpio;
	unsigned long touch_id0_gpio;
	unsigned long touch_id1_gpio;
	const char *mxt_fw_name;
	const char *mxt_cfg_name;
	bool has_cy4x_touch;
	u8 audio_spk_cfg;
	bool is_audio_class_AB;
	bool micvdd_uses_vgp1;
	bool has_i2c_400kHz_limitation;

	/*
	 * NOTE: here GPIO# base is 1 not 0
	* The purpose is to make the default value(0) is not valid
	 */
	s16 gpio_ext_spkamp_en;
	s16 gpio_wifi_combo_bfg_eint;
	s16 gpio_wifi_eint;
	s32 tmp_sensor_params[9];

	s32 car_tune_value;
	bool has_no_tmp103_0x72;

	s32 pmic_sensor_params[3];
	s32 battery_sensor_params[3];
	s32 wmt_sensor_params[3];

	/*
	 * Default value is zero.
	 * Make sure it doen't break Ariel/Aston/Memphis/Thebes
	 */
	u32 audio_flag;
};

#define ALL_REV 0xffff
struct board_hw_cfg_table {
	u16 board_id;
	u16 board_rev;
	char *name;
	struct board_hw_cfg *cfg;
};

extern struct board_hw_cfg *get_hw_cfg(void);
