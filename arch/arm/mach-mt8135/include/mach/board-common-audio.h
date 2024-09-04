
#ifndef __BOARD_COMMON_AUDIO_H__
#define __BOARD_COMMON_AUDIO_H__

struct mt_audio_custom_gpio_data {
	unsigned long combo_i2s_ck;		/* gpio_comobo_i2s_ck_pin */
	unsigned long combo_i2s_ws;		/* gpio_comobo_i2s_ws_pin */
	unsigned long combo_i2s_dat;		/* gpio_comobo_i2s_dat_pin */
	unsigned long pcm_daiout;		/* gpio_pcm_daipcmout_pin */
	unsigned long pmic_ext_spkamp_en;
};

struct mt_audio_platform_data {
	struct mt_audio_custom_gpio_data gpio_data;
	int mt_audio_board_channel_type; /* 0=Stereo, 1=MonoLeft, 2=MonoRight */
};

struct mt_audio_pmic_codec_data {
	int mt_audio_board_amp_class_type; /* 0=class-D, 1=class-AB */
};

int mt_audio_init(struct mt_audio_custom_gpio_data *gpio_data, int mt_audio_board_channel_type,
						int mt_audio_board_amp_class_type);

#endif				/* __BOARD_COMMON_AUDIO_H__ */
