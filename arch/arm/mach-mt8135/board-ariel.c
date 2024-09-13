#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <asm/system_info.h>
#include <mach/mt_board.h>
#include <mach/board-hw-cfg.h>
#include <mach/board.h>
#include <mach/devs.h>
#include <mach/mt_spm_sleep.h>
#include <mach/board-common-audio.h>
#include <mt8135_ariel/cust_gpio_usage.h>
#include <mt8135_ariel/cust_eint.h>
#ifdef CONFIG_MT8135_THEBES
#include <mt8135_thebes/cust_kpd.h>
#else
#include <mt8135_ariel/cust_kpd.h>
#endif

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <uapi/linux/input.h>
#include <linux/cyttsp4_bus.h>
#include <linux/cyttsp4_core.h>
#include <linux/cyttsp4_i2c.h>
#include <linux/cyttsp4_btn.h>
#include <linux/cyttsp4_mt.h>
#ifdef CONFIG_SND_SOC_MAX97236
#include <sound/max97236.h>
#include <mach/mt_pwm.h>
#include <drivers/misc/mediatek/power/mt8135/upmu_common.h>
#endif

#include <linux/lp855x.h>
#include <linux/mpu.h>
#include <uapi/linux/input.h>
#include <linux/platform_data/anx3618.h>

#include <mach/mt_gpio_def.h>
#include <mach/mt_pm_ldo.h>
#include <mach/mt_cam.h>
#include "mt_clk.h"

#ifdef CONFIG_MTK_SERIAL
#include <mach/board-common-serial.h>
#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_RMI4
#include <linux/input/synaptics_dsx.h>

#include <linux/thermal_framework.h>
#include <linux/platform_data/mtk_thermal.h>

#define TM_TYPE1        (1)     /* 2D only */
#define TM_TYPE2        (2)     /* 2D + 0D x 2 */
#define TM_TYPE3        (3)     /* 2D + 0D x 4 */
#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_S7508
#include <linux/input/synaptics_dsx_s7508.h>
#endif
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT
#include <linux/i2c/atmel_mxt_ts.h>
#endif

#ifdef CONFIG_MTK_KEYPAD
#include <mach/mt_typedefs.h>
#include <mach/hal_pub_kpd.h>
#include <mach/mtk_kpd.h>
#endif

#ifdef CONFIG_INPUT_HALL_BU520
#include <linux/input/bu520.h>

#define HALL_1_GPIO_PIN		111
#endif

#include <mach/mt_boot_common.h>

#define USB_IDDIG 34
#define SLIMPORT_IRQ_GPIO 84
#define BACKLIGHT_EN_GPIO	76
#define TOUCH_RST_GPIO	103
#define MAX97236_IRQ_GPIO	102	/* GPIO102 -> EINT10 */
#define TOUCH_IRQ_GPIO	109	/* GPIO109 -> EINT5 */
#define GYRO_IRQ_GPIO	118 /* GPIO118 -> EINT2 */
#define PWM_CLK_PIN 233

#define HP_DET_GPIO     120
#define TOUCH_ID0_GPIO  16  /* GPIO16 -> PMIC KP_COL4 */
#define TOUCH_ID1_GPIO  17  /* GPIO17 -> PMIC KP_COL5 */

#define MSDC3_CLK	200 /* WIFI SDIO CLK */
#define SPI0_CLK	46 /* SPI "WRAPPER PORT" CLK */
#define SENSOR_I2C_PORT 3
#define I2C_FAST_SPEED  400
#define TMP103_NUM_SENSORS 3

#define MTKTSPMIC_TEMP_CRIT 150000      /* 150.000 degree Celsius */
#define MTKTSWMT_TEMP_CRIT 85000

struct lab_i2c_board_info {
	struct i2c_board_info bi;
	int (*init)(struct lab_i2c_board_info *);
};

static int ariel_gpio_to_irq(struct lab_i2c_board_info *info);

static struct board_hw_cfg *hw_cfg;

#ifdef CONFIG_SND_SOC_MAX97236
void max97236_clk_enable(bool enable)
{
	if (enable) {
		mt_pin_set_mode(PWM_CLK_PIN, MT_MODE_1);
		upmu_set_rg_wrp_pwm_pdn(0);
	} else {
		mt_pin_set_mode(PWM_CLK_PIN, MT_MODE_0);
		upmu_set_rg_wrp_pwm_pdn(1);
	}
}

static struct max97236_pdata max97236_platform_data = {
	.irq_gpio = -1,
	.clk_enable = max97236_clk_enable
};

static int ariel_max97236_init(struct lab_i2c_board_info *info)
{
	unsigned gpio_irq = info->bi.irq;
	int err = -ENODEV;

	err = mt_pin_set_mode_eint(gpio_irq);
	if (likely(!err))
		err = mt_pin_set_pull(gpio_irq, MT_PIN_PULL_ENABLE_UP);
	if (likely(!err))
		err = ariel_gpio_to_irq(info);

	if (!gpio_request(HP_DET_GPIO, "max97236")) {
		mt_pin_set_mode(HP_DET_GPIO, MT_MODE_0);
		mt_pin_set_pull(HP_DET_GPIO, MT_PIN_PULL_DISABLE);
		gpio_direction_input(HP_DET_GPIO);
	}

	return err;
}
#endif

extern U32 pmic_config_interface(U32, U32, U32, U32);

static unsigned int get_board_type(void)
{
	unsigned int board_type;

#ifdef CONFIG_IDME
        board_type = idme_get_board_type();
#else
        board_type = BOARD_ID_ARIEL;
#endif
	return board_type;
}

static unsigned int get_board_rev(void)
{
	unsigned int board_rev;
#ifdef CONFIG_IDME
	board_rev = idme_get_board_rev();
#else
	board_rev = BOARD_REV_ARIEL_EVT_1_0;
#endif
	return board_rev;
}


static void touch_power_adjust(enum MT65XX_POWER_VOLCAL vddio, enum MT65XX_POWER_VOLCAL avdd)
{
	/* Use hwPowerCal function in mt_pm_ldo.c. */
	hwPowerCal(MT65XX_POWER_LDO_VGP4, vddio, "touch");

	hwPowerCal(MT65XX_POWER_LDO_VGP6, avdd, "touch");
}

static int touch_power_enable(u16 rst_gpio, bool enable, MT65XX_POWER_VOLTAGE vddio,
			MT65XX_POWER_VOLTAGE avdd)
{
	gpio_direction_output(rst_gpio, 0);
	if (enable) {
		hwPowerOn(MT65XX_POWER_LDO_VGP4, vddio, "touch");
		hwPowerOn(MT65XX_POWER_LDO_VGP6, avdd, "touch");
		msleep(20);

		gpio_set_value(rst_gpio, 1);
	} else {
		hwPowerDown(MT65XX_POWER_LDO_VGP4, "touch");
		hwPowerDown(MT65XX_POWER_LDO_VGP6, "touch");
		gpio_direction_input(rst_gpio);
	}
	return 0;
}

static int touch_power_init(u16 rst_gpio, bool enable)
{
	touch_power_enable(rst_gpio, false, VOL_1800, VOL_3000);
	if (likely(enable)) {
		touch_power_enable(rst_gpio, true, VOL_1800, VOL_3000);
	}
	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT
static int touch_i2c_init_atmel_mxt(struct lab_i2c_board_info *info)
{
	mt_pin_set_mode_gpio(TOUCH_RST_GPIO);

	if (!gpio_request(TOUCH_RST_GPIO, "touch-reset")) {

		/*
		 * Atmel touch spec claims AVDD 3.3V is the normal/standard value.
		 * But it does NOT work withh Atmel 1664/T3 touch IC.
		 * 1066/T2 touch IC seems fine with 3.3V
		 *
		 * We have to set the value above 3.3V.
		 * It seems any values >= 3.34 should work.
		 *
		 * To get the max margin, set it as 3.46V.
		 * 160mV is the max plus voltagge VGP6 could offer.
		 */
		touch_power_adjust(VOLCAL_DEFAULT, VOLCAL_Plus_160);

		touch_power_enable(TOUCH_RST_GPIO, true, VOL_1800, VOL_3300);
		msleep(80);
		gpio_free(TOUCH_RST_GPIO); /* Will be requested in driver.*/
	}

	return 0;

}
#endif


#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_RMI4
static unsigned char TM_TYPE1_f1a_button_codes[] = {};

static int synaptics_rmi4_reset_power(u16 rst_gpio)
{
/* For Aston ESD issue, different timing added for special case */
	gpio_direction_output(rst_gpio, 0);
	msleep(25);
	hwPowerDown(MT65XX_POWER_LDO_VGP4, "touch");
	hwPowerDown(MT65XX_POWER_LDO_VGP6, "touch");

	msleep(100);
	hwPowerOn(MT65XX_POWER_LDO_VGP4, VOL_1800, "touch");
	hwPowerOn(MT65XX_POWER_LDO_VGP6, VOL_3000, "touch");
	msleep(25);
	gpio_set_value(rst_gpio, 1);
	msleep(88);

	return 0;
}

static int synaptics_rmi4_power_enable(u16 rst_gpio, bool enable)
{
	int rc = 0;

	if (enable) {
		rc = touch_power_enable(rst_gpio, enable, VOL_1800, VOL_3000);

		/* For meeting Aston power on spec */
		mt_pin_set_pull(TOUCH_IRQ_GPIO, MT_PIN_PULL_ENABLE_UP);
		msleep(88);
	} else {
		mt_pin_set_pull(TOUCH_IRQ_GPIO, MT_PIN_PULL_DISABLE);
		rc = touch_power_enable(rst_gpio, enable, VOL_1800, VOL_3000);
	}

	return 0;
}

static struct synaptics_rmi_f1a_button_map TM_TYPE1_f1a_button_map = {
	.nbuttons = ARRAY_SIZE(TM_TYPE1_f1a_button_codes),
	.map = TM_TYPE1_f1a_button_codes,
};

static struct synaptics_rmi4_platform_data rmi4_platformdata = {
	.irq_type = IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.gpio = TOUCH_IRQ_GPIO,
	.f1a_button_map = &TM_TYPE1_f1a_button_map,
	.reset_gpio = TOUCH_RST_GPIO,
	.rst_power = synaptics_rmi4_reset_power,
	.power = synaptics_rmi4_power_enable,
};

static int touch_i2c_init_s7301(struct lab_i2c_board_info *info)
{
	mt_pin_set_mode_gpio(TOUCH_RST_GPIO);
	mt_pin_set_pull(TOUCH_IRQ_GPIO, MT_PIN_PULL_ENABLE_UP);

	if (!gpio_request(TOUCH_RST_GPIO, "touch-reset"))
		touch_power_init(TOUCH_RST_GPIO, true);

	return ariel_gpio_to_irq(info);
}

#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_S7508
static int s7508_map[1];
static struct synaptics_dsx_button_map dsx_cap_button_map = {
    .nbuttons = 0,
    .map = s7508_map,
};

static struct synaptics_dsx_button_map dsx_vir_button_map = {
    .nbuttons = 0,
};


static struct synaptics_dsx_board_data dsx_board_data = {
	.x_flip = false,
	.y_flip = false,
	.esd_auto_recover = false,
	.irq_gpio = TOUCH_IRQ_GPIO,
	.irq_on_state = 0,
	.irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
	.power_gpio = -1,
	.reset_gpio = TOUCH_RST_GPIO,
	.power_on_state = 1,
	.reset_on_state = 0,
	.power_delay_ms = 160,
	.reset_delay_ms = 88,
	.reset_active_ms = 300,
	.max_y_for_2d = -1,
	.cap_button_map = &dsx_cap_button_map,
	.vir_button_map = &dsx_vir_button_map,
};

static int touch_i2c_init_s7508(struct lab_i2c_board_info *info)
{

	mt_pin_set_mode_gpio(TOUCH_RST_GPIO);

	if (!gpio_request(TOUCH_RST_GPIO, "touch-reset")) {

		/*
		 * Synatpic touch does not really require this plus 160mV.
		 * However, its init() and Atmel's will be both called.
		 * Atmel touch driver will set this in PMIC anyway for Memphis.
		 *
		 * To make it consistent, we add this code here.
		 */
		touch_power_adjust(VOLCAL_DEFAULT, VOLCAL_Plus_160);

		touch_power_enable(TOUCH_RST_GPIO, true, VOL_1800, VOL_3300);
		msleep(60);
		gpio_free(TOUCH_RST_GPIO); /* Will be requested in driver.*/
	}

	return 0;
}
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4

static int cyttsp4_xres_ariel(struct cyttsp4_core_platform_data *pdata,
				struct device *dev, int delay)
{
	touch_power_enable(pdata->rst_gpio, false, VOL_1800, VOL_3000);

	/*
	 * Config VGP4 to 1800mV Vsel + 100mV Cal
	 * Config VGP6to 3000mV Vsel +100mV Cal
	 */
	touch_power_adjust(VOLCAL_Plus_100, VOLCAL_Plus_100);
	msleep(delay);
	touch_power_enable(pdata->rst_gpio, true, VOL_1800, VOL_3000);

	return 0;
}

static int cyttsp4_power_ariel(struct cyttsp4_core_platform_data *pdata,
				int on, struct device *dev)
{
	return touch_power_enable(pdata->rst_gpio, on, VOL_1800, VOL_3000);
}

/* driver-specific init (power on) */
static int cyttsp4_init_ariel(struct cyttsp4_core_platform_data *pdata,
		     int on, struct device *dev)
{
	int rst_gpio = pdata->rst_gpio;
	int irq_gpio = pdata->irq_gpio;
	int rc = 0;

	if (on) {
		rc = gpio_request(rst_gpio, "touch-reset");
		if (rc < 0) goto err;
		/* touch power init is deferred to cyttsp4_xres_ariel. */
	} else {
		rc = touch_power_init(rst_gpio, false);
		gpio_free(rst_gpio);
		gpio_free(irq_gpio);
	}

err:
	return rc;
}

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_PLATFORM_FW_UPGRADE
#include <linux/cyttsp4_img.h>
static struct cyttsp4_touch_firmware cyttsp4_firmware = {
	.img = cyttsp4_img,
	.size = ARRAY_SIZE(cyttsp4_img),
	.ver = cyttsp4_ver,
	.vsize = ARRAY_SIZE(cyttsp4_ver),
};
#else
static struct cyttsp4_touch_firmware cyttsp4_firmware = {
	.img = NULL,
	.size = 0,
	.ver = NULL,
	.vsize = 0,
};
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_PLATFORM_FW_UPGRADE */

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_PLATFORM_TTCONFIG_UPGRADE
#include <linux/cyttsp4_params.h>
static struct touch_settings cyttsp4_sett_param_regs = {
	.data = (uint8_t *) &cyttsp4_param_regs[0],
	.size = ARRAY_SIZE(cyttsp4_param_regs),
	.tag = 0,
};

static struct touch_settings cyttsp4_sett_param_size = {
	.data = (uint8_t *) &cyttsp4_param_size[0],
	.size = ARRAY_SIZE(cyttsp4_param_size),
	.tag = 0,
};

static struct cyttsp4_touch_config cyttsp4_ttconfig = {
	.param_regs = &cyttsp4_sett_param_regs,
	.param_size = &cyttsp4_sett_param_size,
	.fw_ver = ttconfig_fw_ver,
	.fw_vsize = ARRAY_SIZE(ttconfig_fw_ver),
};
#else
static struct cyttsp4_touch_config cyttsp4_ttconfig = {
	.param_regs = NULL,
	.param_size = NULL,
	.fw_ver = NULL,
	.fw_vsize = 0,
};
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_PLATFORM_TTCONFIG_UPGRADE */

static struct cyttsp4_loader_platform_data _cyttsp4_loader_platform_data = {
	.fw = &cyttsp4_firmware,
	.ttconfig = &cyttsp4_ttconfig,
	.flags = CY_LOADER_FLAG_CHECK_TTCONFIG_VERSION,
};


static struct cyttsp4_core_platform_data _cyttsp4_core_platform_data = {
	.irq_gpio = TOUCH_IRQ_GPIO,
	.rst_gpio = TOUCH_RST_GPIO,
	.xres = cyttsp4_xres_ariel,
	.init = cyttsp4_init_ariel,
	.power = cyttsp4_power_ariel,
	.detect = NULL,
	.flags = CY_CORE_FLAG_NONE,
	.easy_wakeup_gesture = CY_CORE_EWG_NONE,
	.loader_pdata = &_cyttsp4_loader_platform_data,
};

static struct cyttsp4_core_info cyttsp4_core_info = {
	.name = CYTTSP4_CORE_NAME,
	.id = "main_ttsp_core",
	.adap_id = CYTTSP4_I2C_NAME,
	.platform_data = &_cyttsp4_core_platform_data,
};

#define CY_MAXX 800
#define CY_MAXY 1280
#define CY_MINX 0
#define CY_MINY 0

#define CY_ABS_MIN_X CY_MINX
#define CY_ABS_MIN_Y CY_MINY
#define CY_ABS_MAX_X CY_MAXX
#define CY_ABS_MAX_Y CY_MAXY
#define CY_ABS_MIN_P 0
#define CY_ABS_MIN_W 0
#define CY_ABS_MAX_P 255
#define CY_ABS_MAX_W 255

#define CY_ABS_MIN_T 0

#define CY_ABS_MAX_T 15

#define CY_IGNORE_VALUE 0xFFFF

static const uint16_t cyttsp4_abs[] = {
	ABS_MT_POSITION_X, CY_ABS_MIN_X, CY_ABS_MAX_X, 0, 0,
	ABS_MT_POSITION_Y, CY_ABS_MIN_Y, CY_ABS_MAX_Y, 0, 0,
	ABS_MT_PRESSURE, CY_ABS_MIN_P, CY_ABS_MAX_P, 0, 0,
	CY_IGNORE_VALUE, CY_ABS_MIN_W, CY_ABS_MAX_W, 0, 0,
	ABS_MT_TRACKING_ID, CY_ABS_MIN_T, CY_ABS_MAX_T, 0, 0,
};

struct touch_framework cyttsp4_framework = {
	.abs = (uint16_t *) &cyttsp4_abs[0],
	.size = ARRAY_SIZE(cyttsp4_abs),
	.enable_vkeys = 0,
};

static struct cyttsp4_mt_platform_data _cyttsp4_mt_platform_data = {
	.frmwrk = &cyttsp4_framework,
	.flags = CY_MT_FLAG_NO_TOUCH_ON_LO,
	.inp_dev_name = CYTTSP4_MT_NAME,
};

struct cyttsp4_device_info cyttsp4_mt_device_info = {
	.name = CYTTSP4_MT_NAME,
	.core_id = "main_ttsp_core",
	.platform_data = &_cyttsp4_mt_platform_data,
};

/* I2C init method for cypress touch */
static int touch_i2c_init_cypress(struct lab_i2c_board_info *info)
{
	u16 gpio_irq = info->bi.irq;

	mt_pin_set_pull(gpio_irq, MT_PIN_PULL_ENABLE_UP);
	mt_pin_set_mode_gpio(TOUCH_RST_GPIO);

	cyttsp4_register_core_device(&cyttsp4_core_info);
	cyttsp4_register_device(&cyttsp4_mt_device_info);

	// gpio to irq conversion has to be done before i2c registration
	return ariel_gpio_to_irq(info);
}
#endif				/* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4 */
/********** keypad Customization ************************/
#ifdef CONFIG_MTK_KEYPAD

static u16 kpd_keymap[KPD_NUM_KEYS] = KPD_INIT_KEYMAP();

struct mtk_kpd_hardware mtk_kpd_hw = {
	.kpd_init_keymap = kpd_keymap,
	.kpd_pwrkey_map = KEY_POWER,
	.kpd_key_debounce = KPD_KEY_DEBOUNCE,
	.onekey_reboot_normal_mode = TRUE,	/* ONEKEY_REBOOT_NORMAL_MODE */
	.twokey_reboot_normal_mode = FALSE,	/* TWOKEY_REBOOT_NORMAL_MODE */
	.onekey_reboot_other_mode = TRUE,	/* ONEKEY_REBOOT_OTHER_MODE */
	.twokey_reboot_other_mode = FALSE,	/* TWOKEY_REBOOT_OTHER_MODE */
	.kpd_pmic_rstkey_map_en = FALSE,	/* KPD_PMIC_RSTKEY_MAP */
	.kpd_pmic_rstkey_map_value = KEY_VOLUMEDOWN,
	.kpd_pmic_lprst_td_en = TRUE,	/* KPD_PMIC_LPRST_TD */
	.kpd_pmic_lprst_td_value = 2,	/* timeout period. 0: 8sec; 1: 11sec; 2: 14sec; 3: 5sec */
	.kcol = {
		[0] = GPIO_KPD_KCOL0_PIN & ~0x80000000,
		[1] = GPIO_KPD_KCOL1_PIN & ~0x80000000,
	},
};

static struct platform_device kpd_pdev = {
	.name = "mtk-kpd",
	.id = -1,
	.dev = {
		.platform_data = &mtk_kpd_hw,
	},
};

static int __init mt_kpd_init(void)
{
	return platform_device_register(&kpd_pdev);
}

#endif				/* CONFIG_MTK_KEYPAD */

/********************* Camera Customize ***************************/
struct mt_camera_sensor_config ariel_camera_sensor_conf[] = {
	{
		.name = "ov2724mipiraw",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 184, /* CMPDN */
		.pdn_on = 0,
		.rst_pin = 183,	/*CM_RST */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = MT65XX_POWER_LDO_VCAMIO,
		.vcam_io = -1,
	},
	{
		.name = "hi704yuv",
		.mclk_name = "CMMCLK",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 73, /* OK */
		.pdn_on = 1,
		.rst_pin = 74, /* OK; not connected */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = MT65XX_POWER_LDO_VCAMIO,
		.vcam_io = -1,
	},
};

struct mt_camera_sensor_config aston_proto_camera_sensor_conf[] = {
	{
		.name = "ov2724mipiraw",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 184, /* CMPDN */
		.pdn_on = 0,
		.rst_pin = 183,	/*CM_RST */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = MT65XX_POWER_LDO_VCAMIO,
		.vcam_io = -1,
	},
	{
		.name = "hi704yuv",
		.mclk_name = "CMMCLK",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 73, /* OK */
		.pdn_on = 1,
		.rst_pin = 74, /* OK; not connected */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMD,
		.vcam_d = MT65XX_POWER_LDO_VCAMAF,
		.vcam_io = MT65XX_POWER_LDO_VCAMIO,	/* i2c power */
	},
};

struct mt_camera_sensor_config aston_evt_camera_sensor_conf[] = {
	{
		.name = "ov2724mipiraw",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 184, /* CMPDN */
		.pdn_on = 0,
		.rst_pin = 183,	/*CM_RST */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = MT65XX_POWER_LDO_VCAMIO,
		.vcam_io = -1,
	},
	{
		.name = "hi704yuv",
		.mclk_name = "CMMCLK",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 73, /* OK */
		.pdn_on = 1,
		.rst_pin = 74, /* OK; not connected */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = MT65XX_POWER_LDO_VCAMIO,
		.vcam_io = -1,
	},
};

struct mt_camera_sensor_config s5k5e_camera_sensor_conf[] = {
	{
		.name = "s5k5e2yamipiraw",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 184, /* CMPDN */
		.pdn_on = 0,
		.rst_pin = 183,	/*CM_RST */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = MT65XX_POWER_LDO_VGP5,
		.vcam_io = MT65XX_POWER_LDO_VCAMIO,
	},
};

struct mt_camera_sensor_config ov9724_camera_sensor_conf[] = {
	{
		.name = "ov9724mipiraw",
		.mclk_name = "CMMCLK",
		.auxclk_name = "ISP_MCLK1",
		.pdn_pin = 73, /* OK */
		.pdn_on = 0,
		.rst_pin = 74, /* OK; not connected */
		.rst_on = 0,
		.vcam_a = MT65XX_POWER_LDO_VCAMA,
		.vcam_d = -1,
		.vcam_io = MT65XX_POWER_LDO_VCAMIO,
	},
};

/********************* ATMEL TouchScreen Customize ***************************/
#ifdef CONFIG_TOUCHSCREEN_MTK_ATMEL_MXT
#include <drivers/input/touchscreen/atmel_mxt_ts/atmel_mxt_ts.h>

static struct mxt_platform_data atmel_pdata = {
	.reset_gpio = TOUCH_RST_GPIO,
	.irq_gpio = TOUCH_IRQ_GPIO,
};
#endif

/* WiFi SDIO platform data */
static struct mtk_wifi_sdio_data ariel_wifi_sdio_data = {
	.irq = MTK_EINT_PIN(WIFI_EINT, EINTF_TRIGGER_LOW),
};

/* HDMI platform data */
#ifdef CONFIG_MTK_INTERNAL_HDMI_SUPPORT
static const struct mtk_hdmi_data ariel_hdmi_data = {
	.pwr = MTK_GPIO(HDMI_POWER_CONTROL),
};
#endif

/************************* Audio Customization ******************************/

static struct mt_audio_custom_gpio_data audio_custom_gpio_data = {
	.combo_i2s_ck = GPIO_COMBO_I2S_CK_PIN & ~0x80000000,
	.combo_i2s_ws = GPIO_COMBO_I2S_WS_PIN & ~0x80000000,
	.combo_i2s_dat = GPIO_COMBO_I2S_DAT_PIN & ~0x80000000,
	.pcm_daiout = GPIO_PCM_DAIPCMOUT_PIN & ~0x80000000,
	.pmic_ext_spkamp_en = ARCH_NR_GPIOS,
};

/*****************************************************************************/
/*****************************************************************************/

struct mtk_pmic_eint cust_pmic_eint = {
	.irq = MTK_EINT_PIN(PMIC_EINT, EINTF_TRIGGER_HIGH),
};

struct mtk_thermal_platform_data mtktspmic_thermal_data = {
	.num_trips = 1,
	.mode = THERMAL_DEVICE_DISABLED,
	.polling_delay = 1000,
	.trips[0] = {.temp = MTKTSPMIC_TEMP_CRIT, .type = THERMAL_TRIP_CRITICAL, .hyst = 0},
};

struct mtk_thermal_platform_data_wrapper mtktspmic_thermal_data_wrapper = {
	.data = &mtktspmic_thermal_data,
};

struct platform_device mtktspmic_device = {
	.name = "mtktspmic",
	.id = -1,
	.dev = {
		.platform_data = &mtktspmic_thermal_data_wrapper,
	},
};

struct platform_device pmic_mt6397_device = {
	.name = "pmic_mt6397",
	.id = -1,
	.dev = {
		.platform_data = &cust_pmic_eint,
		}
};

void __init mt_pmic_init(void)
{
	int ret;

	ret = platform_device_register(&pmic_mt6397_device);
	if (ret) {
		pr_info(
		"Power/PMIC****[pmic_mt6397_init] Unable to device register(%d)\n", ret);
	}
	mtktspmic_thermal_data_wrapper.params.offset = hw_cfg->pmic_sensor_params[0];
	mtktspmic_thermal_data_wrapper.params.alpha = hw_cfg->pmic_sensor_params[1];
	mtktspmic_thermal_data_wrapper.params.weight = hw_cfg->pmic_sensor_params[2];
	ret = platform_device_register(&mtktspmic_device);
	if (ret)
		pr_err("Unable to register mtktspmic device (%d)\n", ret);
}

struct mtk_thermal_platform_data_wrapper mtktswmt_thermal_data_wrapper = {};

struct mtk_thermal_platform_data mtktswmt_thermal_data = {
	.num_trips = 1,
	.mode = THERMAL_DEVICE_DISABLED,
	.polling_delay = 1000,
	.trips[0] = {.temp = MTKTSWMT_TEMP_CRIT, .type = THERMAL_TRIP_CRITICAL, .hyst = 0},
};

struct platform_device mtktswmt_device = {
	.name = "mtktswmt",
	.id = -1,
	.dev = {
		.platform_data = &mtktswmt_thermal_data_wrapper,
	},
};

int __init mt_wmt_init(void)
{
	int ret;

	mtktswmt_thermal_data_wrapper.data = &mtktswmt_thermal_data;
	mtktswmt_thermal_data_wrapper.params.offset = hw_cfg->wmt_sensor_params[0];
	mtktswmt_thermal_data_wrapper.params.alpha = hw_cfg->wmt_sensor_params[1];
	mtktswmt_thermal_data_wrapper.params.weight = hw_cfg->wmt_sensor_params[2];

	ret = platform_device_register(&mtktswmt_device);
	if (ret) {
		pr_err("Unable to register mtktswmt device (%d)\n", ret);
		return ret;
	}
	return 0;
}


#if 0
static int ariel_gpio_to_eint(struct lab_i2c_board_info *info)
{
	u16 gpio_irq = info->bi.irq;
	struct mt_pin_def *pin = mt_get_pin_def(gpio_irq);
	int err = -ENODEV;
	if (pin && pin->eint) {
		err = mt_pin_set_mode_eint(gpio_irq);
		if (!err)
			err = gpio_request(gpio_irq, info->bi.type);
		if (!err)
			err = gpio_direction_input(gpio_irq);
		/* TODO: this is incorrect translation, but compatible
		 * with MTK mt_eint_* code; update, when mt_eint cleanup
		 * is done */
		if (!err)
			info->bi.irq = pin->eint->id;
		else
			info->bi.irq = -1;
	}
	return err;
}
#endif

static void ariel_gpio_init(int gpio, u32 flags, int pull_mode)
{
	if (gpio > 0) {
		int err;
		mt_pin_set_mode_gpio(gpio);
		mt_pin_set_pull(gpio, pull_mode);
		err = gpio_request_one(gpio, flags, "ariel");
		pr_err("GPIO%d: board init done; err=%d\n", gpio, err);
		gpio_free(gpio);
	}
}

static int ariel_gpio_to_irq(struct lab_i2c_board_info *info)
{
	u16 gpio_irq = info->bi.irq;
	int err = mt_pin_set_mode_eint(gpio_irq);
	if (!err)
		err = gpio_request(gpio_irq, info->bi.type);
	if (!err)
		err = gpio_direction_input(gpio_irq);
	if (!err)
		info->bi.irq = gpio_to_irq(gpio_irq);
	if (!err && info->bi.irq < 0)
		err = info->bi.irq;
	if (err)
		info->bi.irq = -1;
	return err;
}

static struct lp855x_rom_data ariel_lp8557_eeprom_arr[] = {
	{0x14, 0xCF}, /* 4V OV, 4 LED string enabled */
	{0x13, 0x01}, /* Boost frequency @1MHz */
};

static struct lp855x_rom_data lp8557_eeprom_s4_vmax25[] = {
	{0x14, 0xCF}, /* 4V OV, 4 LED string enabled */
	{0x13, 0x01}, /* Boost frequency @1MHz */
	{0x11, 0x06}, /* full scale 23 mA, but user is not allowed to set full scale */
	{0x12, 0x2B}, /* 19.5 kHz */
	{0x15, 0x42}, /* light smoothing, 100 ms */
	{0x16, 0xA0}, /* Max boost voltage 25V, enable adaptive voltage */
};

static struct lp855x_rom_data lp8557_eeprom_s4_vmax22_23ma[] = {
	{0x14, 0xCF}, /* 4V OV, 4 LED string enabled */
	{0x13, 0x01}, /* Boost frequency @1MHz */
	{0x11, 0x06}, /* full scale 23 mA, but user is not allowed to set full scale */
	{0x12, 0x2B}, /* 19.5 kHz */
	{0x15, 0x42}, /* light smoothing, 100 ms */
	{0x16, 0x60}, /* Max boost voltage 22V, enable adaptive voltage */
};

#define lp8557_5leds_rom aston_lp8557_eeprom_arr
static struct lp855x_rom_data aston_lp8557_eeprom_arr[] = {
	{0x14, 0xDF}, /* 4V OV, 5 LED string enabled */
	{0x13, 0x01}, /* Boost frequency @1MHz */
};



static struct lab_i2c_board_info sy_7508_touch_dev = {
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_S7508
    .bi = {
        I2C_BOARD_INFO(I2C_DRIVER_NAME, 0x20),
        .irq = TOUCH_IRQ_GPIO,
        .platform_data = &dsx_board_data,
     },
     .init = touch_i2c_init_s7508,
#endif
};

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT
static int atmel_mxt_reset_power(void);
static int atmel_mxt_power(bool on);

static struct mxt_platform_data atmel_mxt_pdata = {
	.gpio_reset = TOUCH_RST_GPIO,
	.irqflags = IRQF_TRIGGER_LOW,
	.rst_power = atmel_mxt_reset_power,
	.power = atmel_mxt_power,
	.esd_auto_recover = false,
};
static struct lab_i2c_board_info atmel_mxt_dev = {
    .bi = {
		/* FIXME - revisit the i2c addr 0x4C */
        I2C_BOARD_INFO("atmel_mxt_ts", 0x4A),
		/* FIXME - revisit the gpio_irq, this will be changed by ariel_gpio_to_irq */
        .irq = TOUCH_IRQ_GPIO,
        .platform_data = &atmel_mxt_pdata,
     },
     .init = touch_i2c_init_atmel_mxt,
};
static int atmel_mxt_reset_power(void)
{
	if (atmel_mxt_pdata.gpio_reset >= 0) {
		while (hwPowerCount(MT65XX_POWER_LDO_VGP4) > 0)
			hwPowerDown(MT65XX_POWER_LDO_VGP4, "touch");
		while (hwPowerCount(MT65XX_POWER_LDO_VGP6) > 0)
			hwPowerDown(MT65XX_POWER_LDO_VGP6, "touch");
		msleep(100);
		atmel_mxt_dev.init(&atmel_mxt_dev);
	}
	return 0;
}

static int atmel_mxt_power(bool on)
{
	if (on)
		atmel_mxt_dev.init(&atmel_mxt_dev);
	else {
		while (hwPowerCount(MT65XX_POWER_LDO_VGP4) > 0)
			hwPowerDown(MT65XX_POWER_LDO_VGP4, "touch");
		while (hwPowerCount(MT65XX_POWER_LDO_VGP6) > 0)
			hwPowerDown(MT65XX_POWER_LDO_VGP6, "touch");
		mt_pin_set_mode_gpio(TOUCH_RST_GPIO);
		if (!gpio_request(TOUCH_RST_GPIO, "touch-reset")) {
			mt_pin_set_pull(TOUCH_RST_GPIO, MT_PIN_PULL_ENABLE_DOWN);
			gpio_direction_input(TOUCH_RST_GPIO);
			gpio_free(TOUCH_RST_GPIO);
		}
		usleep_range(5000, 5500);
	}
	return 0;
}

static void atmel_mxt_config_update(void)
{
	if (hw_cfg->mxt_fw_name != NULL) {
		atmel_mxt_pdata.fw_name = kstrdup(hw_cfg->mxt_fw_name, GFP_KERNEL);
		if (!atmel_mxt_pdata.fw_name)
			pr_err("ENOMEM for fw_name allocation");
	}
	if (hw_cfg->mxt_cfg_name != NULL) {
		if (hw_cfg->use_touch_id_gpio) {
			int id0, id1;
			int cfg_len = strlen(hw_cfg->mxt_cfg_name) + strlen("**") + 1;
			atmel_mxt_pdata.cfg_name = kmalloc(cfg_len, GFP_KERNEL);
			if (NULL != atmel_mxt_pdata.cfg_name) {
				strlcpy(atmel_mxt_pdata.cfg_name,
					hw_cfg->mxt_cfg_name, cfg_len);
				ariel_gpio_init(hw_cfg->touch_id0_gpio,
					GPIOF_DIR_IN, MT_PIN_PULL_ENABLE_UP);
				ariel_gpio_init(hw_cfg->touch_id1_gpio,
					GPIOF_DIR_IN, MT_PIN_PULL_ENABLE_UP);
				usleep_range(5000, 6000);
				id0 = gpio_get_value(hw_cfg->touch_id0_gpio);
				id1 = gpio_get_value(hw_cfg->touch_id1_gpio);
				strlcat(atmel_mxt_pdata.cfg_name, id0 ? "1" : "0", cfg_len);
				strlcat(atmel_mxt_pdata.cfg_name, id1 ? "1" : "0", cfg_len);
				mt_pin_set_pull(hw_cfg->touch_id0_gpio, MT_PIN_PULL_DISABLE);
				mt_pin_set_pull(hw_cfg->touch_id1_gpio, MT_PIN_PULL_DISABLE);
				pr_info("touch id0 = %d, id1 = %d\n", id0, id1);
			} else
				pr_err("ENOMEM for cfg_name allocation");
		} else {
			atmel_mxt_pdata.cfg_name = kstrdup(hw_cfg->mxt_cfg_name, GFP_KERNEL);
			if (!atmel_mxt_pdata.cfg_name)
				pr_err("ENOMEM for cfg_name allocation");

		}
	}
}
#endif


static int ariel_mpu_init(struct lab_i2c_board_info *info)
{
	unsigned gpio_irq = info->bi.irq;
	int err;
	/* EINT is not used; use PD mode to save power */
	err = mt_pin_set_mode_gpio(gpio_irq);
	if (!err)
		err = mt_pin_set_pull(gpio_irq, MT_PIN_PULL_ENABLE_DOWN);
	if (!err)
		err = ariel_gpio_to_irq(info);
	return err;
}

static struct mpu_platform_data mpu_data = {
	.orientation = {
		1, 0, 0,
		0, 1, 0,
		0, 0, 1
	},
	.secondary_orientation = {
		0, 1, 0,
		1, 0, 0,
		0, 0, -1
	},
};

#if 0
static struct mpu_platform_data mpu_data_aston = {
	.orientation = {
		-1,  0,  0,
		0,  1,  0,
		0,  0,  -1
	},
	.secondary_orientation = {
		0,  1,  0,
		1,  0,  0,
		0,  0, -1
	},
};
#endif


/*
 * FIXME: This has got to go away.
 *
 * GPIO87 can be configured to CLKMGR mode
 * Set 1 to bit 16 of the undocumented address 0xF0000220
 * enables 26MHz clock out put according to MTK
 */
static void ariel_slimport_clk_enable(bool enable)
{
	if (enable)
		writel(0x10000, (void __iomem *)0xF0000220);
	else
		writel(0x0, (void __iomem *)0xF0000220);
}

static struct anx3618_platform_data anx3618_gpioclk_data = {
	.gpio_hdmi      = 114,
	.gpio_vmch	= 115,
	.gpio_usb_plug  = 64,
	.gpio_pd        = 85,
	.gpio_rst       = 83,
	.gpio_cable_det = SLIMPORT_IRQ_GPIO,
	.gpio_intp      = 88,
	.gpio_usb_det   = 86,
	.gpio_1_0_v_on  = 82,
	.gpio_extclk	= 87,
	.vreg_3_3v      = MT65XX_POWER_LDO_VMCH,
	.vreg_gpio_1_0v = MT65XX_POWER_LDO_VMC,
	.vreg_gpio_1_0v_config = VOL_1800,
	.anx3618_clk_enable = ariel_slimport_clk_enable,
};

static struct anx3618_platform_data anx3618_data = {
	.gpio_hdmi      = 114,
	.gpio_vmch	= 115,
	.gpio_usb_plug  = 64,
	.gpio_pd        = 85,
	.gpio_rst       = 83,
	.gpio_cable_det = SLIMPORT_IRQ_GPIO,
	.gpio_intp      = 88,
	.gpio_usb_det   = 86,
	.gpio_sel_uart  = 87,
	.gpio_1_0_v_on  = 82,
	.vreg_3_3v      = MT65XX_POWER_LDO_VMCH,
	.vreg_gpio_1_0v = MT65XX_POWER_LDO_VMC,
	.vreg_gpio_1_0v_config = VOL_1800,
};

static int anx_init(struct lab_i2c_board_info *info)
{
	struct anx3618_platform_data *pdata = info->bi.platform_data;
	ariel_gpio_init(pdata->gpio_1_0_v_on,
		GPIOF_DIR_OUT,	MT_PIN_PULL_DISABLE);
	ariel_gpio_init(pdata->gpio_rst,
		GPIOF_DIR_OUT,	MT_PIN_PULL_DISABLE);
	ariel_gpio_init(pdata->gpio_hdmi,
		GPIOF_DIR_OUT,	MT_PIN_PULL_DISABLE);
	ariel_gpio_init(pdata->gpio_vmch,
		GPIOF_DIR_OUT,	MT_PIN_PULL_DISABLE);
	ariel_gpio_init(pdata->gpio_usb_plug,
		GPIOF_DIR_OUT,	MT_PIN_PULL_DISABLE);
	ariel_gpio_init(pdata->gpio_pd,
		GPIOF_DIR_OUT,	MT_PIN_PULL_DISABLE);
	ariel_gpio_init(pdata->gpio_sel_uart,
		GPIOF_DIR_IN,	MT_PIN_PULL_ENABLE_DOWN);
	ariel_gpio_init(pdata->gpio_cable_det,
		GPIOF_DIR_IN,	MT_PIN_PULL_ENABLE_UP);
	ariel_gpio_init(pdata->gpio_intp,
		GPIOF_DIR_IN,	MT_PIN_PULL_ENABLE_UP);
	ariel_gpio_init(pdata->gpio_usb_det,
		GPIOF_DIR_IN,	MT_PIN_PULL_DISABLE);
	ariel_gpio_to_irq(info);

	/* FIXME: Set GPIO87 to MODE5 for CLKMGR */
	if (pdata->gpio_extclk == 87) {
		mt_pin_set_mode_by_name(pdata->gpio_extclk, "CLKM");
		mt_pin_set_load(pdata->gpio_extclk, 12, true);
	}
	mt_pin_set_mode_eint(pdata->gpio_intp);
	return 0;
}


struct i2c_board_cfg {
	struct lab_i2c_board_info *info;
	char *name;
	u16	speed;
	u8	bus_id;
	u8  n_dev;
	u16	min_rev;
	u16 max_rev;
	u16 *wrrd;
};

#define LAB_I2C_BUS_CFG(_bus_id, _bus_devs, _bus_speed) \
.name = #_bus_devs, \
.info = (_bus_devs), \
.bus_id = (_bus_id), \
.speed = (_bus_speed), \
.n_dev = ARRAY_SIZE(_bus_devs)

/* both aston and ariel */
static u16 i2c_bus2_wrrd[] = {
	0x40, /* MAX97236 needs WRRD (I2C repeated start) */
	0
};


static struct lab_i2c_board_info anx3618_dev = {
	.bi = {
		I2C_BOARD_INFO("anx3618", 0x39),
		.irq = SLIMPORT_IRQ_GPIO,
		.platform_data = &anx3618_data,
	},
	.init = anx_init,
};

static struct lp855x_platform_data lp8557_pdata = {
	.mode = REGISTER_BASED,
	.device_control = (LP8557_COMB2_CONFIG | LP8557_PWM_FILTER | LP8557_DISABLE_LEDS),
	.initial_brightness = 0x64,
	.max_brightness = 0xFF,
	.brightness_limit = 0,
	.load_new_rom_data = 1,
	.size_program = ARRAY_SIZE(ariel_lp8557_eeprom_arr),
	.rom_data = ariel_lp8557_eeprom_arr,
	.gpio_en = BACKLIGHT_EN_GPIO,
	.cool_dev = {
		.type = "lcd-backlight",
		.state = 0,
		.max_state = THERMAL_MAX_TRIPS,
		.level = 0,
		.levels = {
			175, 175, 175,
			175, 175, 175,
			175, 175, 175,
			175, 175, 175
		},
	},
};

static struct i2c_board_info lp8557_bl_dev = {
	I2C_BOARD_INFO("lp8557_led", 0x2C),
	.platform_data = &lp8557_pdata,
};

static struct lab_i2c_board_info icm20645_sens_dev = {
	.bi = {
		I2C_BOARD_INFO("icm20645", 0x68),
		.irq = GYRO_IRQ_GPIO,
		.platform_data = &mpu_data,
	},
	.init = ariel_mpu_init,
};

static struct lab_i2c_board_info cy_touch_dev = {
	.bi = {
		I2C_BOARD_INFO(CYTTSP4_I2C_NAME, 0x24),
		.irq = TOUCH_IRQ_GPIO,
		.platform_data = CYTTSP4_I2C_NAME,
	},
	.init = touch_i2c_init_cypress,
};

static struct lab_i2c_board_info mpu6515_dev = {
	.bi = {
		I2C_BOARD_INFO("mpu6515", 0x68),
		.irq = GYRO_IRQ_GPIO,
		.platform_data = &mpu_data,
	},
	.init = ariel_mpu_init,
};

static struct lab_i2c_board_info sy_7301_touch_dev = {
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_RMI4
		.bi = {
			I2C_BOARD_INFO("synaptics_rmi4_i2c", 0x20),
			.irq = TOUCH_IRQ_GPIO,
			.platform_data = &rmi4_platformdata,
		},
		.init = touch_i2c_init_s7301,
#endif
};


static struct i2c_board_info tmp103_dev0 = {
			I2C_BOARD_INFO("tmp103_temp_sensor", 0x70),
};

static struct i2c_board_info tmp103_dev1 = {
			I2C_BOARD_INFO("tmp103_temp_sensor", 0x71),
};

static struct i2c_board_info tmp103_dev2 = {
			I2C_BOARD_INFO("tmp103_temp_sensor", 0x72),
};

static struct lab_i2c_board_info hs_amp_dev = {
#ifdef CONFIG_SND_SOC_MAX97236
		.bi = {
			I2C_BOARD_INFO("max97236", 0x40),
			.irq = MAX97236_IRQ_GPIO,
			.platform_data = &max97236_platform_data,
		},
		.init = ariel_max97236_init,
#endif
};

static int __init setup_lp855x_brightness_limit(char *str)
{
	int bl_limit;

	if (get_option(&str, &bl_limit))
		lp8557_pdata.brightness_limit = bl_limit;
	return 0;
}
__setup("lp855x_bl_limit=", setup_lp855x_brightness_limit);

static void i2c_bus_config(void)
{
	if (hw_cfg->has_i2c_400kHz_limitation) {
		/* there is HW limitation to use 400khz i2c bus in Ariel/Aston */
		mt_dev_i2c_bus_configure(0, 100, 0);
		mt_dev_i2c_bus_configure(1, 100, 0);
	} else {
		/* high speed mode for touch & camera */
		mt_dev_i2c_bus_configure(0, 350, 0);
		mt_dev_i2c_bus_configure(1, 350, 0);
	}
	mt_dev_i2c_bus_configure(2, 100, i2c_bus2_wrrd);
	mt_dev_i2c_bus_configure(3, 100, 0 );
	mt_dev_i2c_bus_configure(5, 300, 0);
}

static int i2c_devs_init(void)
{

	if (hw_cfg == NULL)
		return -1;

	i2c_bus_config();

	/* Do not init unneeded devices in the recovery safe mode */
	if (g_boot_mode != RECOVERY_BOOT) {

		if (hw_cfg->micvdd_uses_vgp1)
			hwPowerOn(MT65XX_POWER_LDO_VCAMD, VOL_2800, "Max97236");

		if (hw_cfg->audio_flag)
			((struct max97236_pdata *)hs_amp_dev.bi.platform_data)->jack_sw_normal_open = true;
		else
			((struct max97236_pdata *)hs_amp_dev.bi.platform_data)->jack_sw_normal_open = false;

		hs_amp_dev.init(&hs_amp_dev);
		i2c_register_board_info(2, &(hs_amp_dev.bi), 1);

		if (hw_cfg->has_slimport) {
			if (hw_cfg->has_slimport_gpioclk)
				anx3618_dev.bi.platform_data = &anx3618_gpioclk_data;

			anx3618_dev.init(&anx3618_dev);
			i2c_register_board_info(5, &(anx3618_dev.bi), 1);
		}

		if (hw_cfg->has_icm20645) {

			if (hw_cfg->sensor_orient[0] || hw_cfg->sensor_orient[1] ||
				hw_cfg->sensor_orient[2]) {
				memcpy(mpu_data.orientation, hw_cfg->sensor_orient,
						ARRAY_SIZE(mpu_data.orientation) * sizeof(s8));
				memcpy(mpu_data.secondary_orientation, hw_cfg->sec_sensor_orient,
						ARRAY_SIZE(mpu_data.secondary_orientation) * sizeof(s8));
			}

			mt_dev_i2c_bus_configure(SENSOR_I2C_PORT, 350, 0);
			ariel_mpu_init(&icm20645_sens_dev);
			i2c_register_board_info(3, &(icm20645_sens_dev.bi), 1);
		}

		if (hw_cfg->has_mpu6515) {

			if (hw_cfg->sensor_orient[0] || hw_cfg->sensor_orient[1] ||
				hw_cfg->sensor_orient[2]) {
				memcpy(mpu_data.orientation, hw_cfg->sensor_orient,
						ARRAY_SIZE(mpu_data.orientation) * sizeof(s8));
				memcpy(mpu_data.secondary_orientation, hw_cfg->sec_sensor_orient,
						ARRAY_SIZE(mpu_data.secondary_orientation) * sizeof(s8));
			}

			mpu6515_dev.init(&mpu6515_dev);
			i2c_register_board_info(3, &(mpu6515_dev.bi), 1);
		}
	}

	if (hw_cfg->has_cy_touch) {
		cy_touch_dev.init(&cy_touch_dev);
		i2c_register_board_info(0, &(cy_touch_dev.bi), 1);
	}

	if (hw_cfg->has_sy_7508_touch) {
		if (hw_cfg->touch_auto_recover)
			dsx_board_data.esd_auto_recover = true;
		sy_7508_touch_dev.init(&sy_7508_touch_dev);
		i2c_register_board_info(0, &(sy_7508_touch_dev.bi), 1);
	}

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT
	if (hw_cfg->has_atmel_mxt_touch) {
		if (hw_cfg->touch_auto_recover)
			atmel_mxt_pdata.esd_auto_recover = true;
		i2c_register_board_info(0, &(atmel_mxt_dev.bi), 1);
		atmel_mxt_config_update();
	}
#endif

	if (hw_cfg->has_sy_7301_touch) {
		sy_7301_touch_dev.init(&sy_7301_touch_dev);
		i2c_register_board_info(0, &(sy_7301_touch_dev.bi), 1);
	}

	if (!(hw_cfg->has_no_tmp103)) {
		tmp103_dev0.platform_data = (struct thermal_dev_params *)(&hw_cfg->tmp_sensor_params[0]);
		tmp103_dev1.platform_data = (struct thermal_dev_params *)(&hw_cfg->tmp_sensor_params[3]);
		tmp103_dev2.platform_data = (struct thermal_dev_params *)(&hw_cfg->tmp_sensor_params[6]);
		i2c_register_board_info(2, &(tmp103_dev0), 1);
		i2c_register_board_info(2, &(tmp103_dev1), 1);

		if (!hw_cfg->has_no_tmp103_0x72)
			i2c_register_board_info(2, &(tmp103_dev2), 1);
	}

	if (hw_cfg->lp8557_dev_ctrl) {
		lp8557_pdata.device_control = hw_cfg->lp8557_dev_ctrl;

#ifdef CONFIG_IDME
		if (idme_get_bootmode() == BOOT_MODE_DIAG) /* to filter LED defect easily in factory.*/
			lp8557_pdata.device_control &= ~LP8557_DISABLE_LEDS;
#endif
	}

	if (hw_cfg->lp8557_has_5leds)
		lp8557_pdata.rom_data = lp8557_5leds_rom;

	if (hw_cfg->lp8557_has_4leds_vmax25) {
		lp8557_pdata.rom_data = lp8557_eeprom_s4_vmax25;
		lp8557_pdata.size_program = ARRAY_SIZE(lp8557_eeprom_s4_vmax25);
	}

	if (hw_cfg->lp8557_has_4leds_vmax22_23ma) {
		lp8557_pdata.rom_data = lp8557_eeprom_s4_vmax22_23ma;
		lp8557_pdata.size_program = ARRAY_SIZE(lp8557_eeprom_s4_vmax22_23ma);
	}

	i2c_register_board_info(2, &lp8557_bl_dev, 1);

	return 0;
}

struct mt_pinctrl_cfg ariel_pin_cfg[] = {
	{ .gpio = USB_IDDIG, .pull = MT_PIN_PULL_DISABLE, .mode = MT_MODE_MAIN, },
	{ .gpio = MSDC3_CLK, .load_ma = 6, .mode = MT_MODE_MAIN, },
	{}
};

#ifdef CONFIG_INPUT_HALL_BU520
static struct hall_sensor_data hall_sensor_data[] = {
	{
	 .gpio_pin = HALL_1_GPIO_PIN,
	 .name = "hall_sensor_1",
	 .gpio_state = 1,
	},
};
static struct hall_sensors hall_sensors = {
	.hall_sensor_data = hall_sensor_data,
	.hall_sensor_num = ARRAY_SIZE(hall_sensor_data),
};
static struct platform_device hall_bu520_device = {
	.name = "hall-bu520",
	.id = -1,
	.dev = {
		.platform_data = &hall_sensors,
		}
};

static void __init hall_bu520_init(void)
{
	int i;
	int gpio_pin;

	for (i = 0; i < hall_sensors.hall_sensor_num; i++) {
		gpio_pin = hall_sensors.hall_sensor_data[i].gpio_pin;
		mt_pin_set_mode_eint(gpio_pin);
		mt_pin_set_pull(gpio_pin, MT_PIN_PULL_ENABLE_UP);
		gpio_request(gpio_pin, hall_sensors.hall_sensor_data[i].name);
		gpio_direction_input(gpio_pin);
	}
	platform_device_register(&hall_bu520_device);
}
#endif

#ifdef CONFIG_MTK_SERIAL
/*********************** uart setup ******************************/
static struct mtk_uart_mode_data mt_uart1_data = {
	.uart = {
		.txd = MTK_GPIO_PIN(UART_UTXD1),
		.rxd = MTK_GPIO_PIN(UART_URXD1),
	},
};

static struct mtk_uart_mode_data mt_uart2_data = {
	.uart = {
		.txd = MTK_GPIO_PIN(UART_UTXD2),
		.rxd = MTK_GPIO_PIN(UART_URXD2),
	},
};

static struct mtk_uart_mode_data mt_uart3_data = {
	.uart = {
		.txd = MTK_GPIO_PIN(UART_UTXD3),
		.rxd = MTK_GPIO_PIN(UART_URXD3),
	},
};

static struct mtk_uart_mode_data mt_uart4_data = {
	.uart = {
	/*	.txd = MTK_GPIO_PIN(UART_UTXD4), */
		.rxd = MTK_GPIO_PIN(UART_URXD4),
	},
};

static void __init mt_ariel_serial_init(void)
{
	mt_register_serial(0, &mt_uart1_data);
	mt_register_serial(1, &mt_uart2_data);
	mt_register_serial(2, &mt_uart3_data);
	mt_register_serial(3, &mt_uart4_data);
}
#endif

static struct eint_domain_config ariel_eint_config[EINT_DOMAIN_NUM] = {
	{ .start = 0, .size = 32, },
	{ .start = 32, .size = 32, },
	{ .start = 192, .size = 25, },
};

/* AP: NOTE:
 *
 * this macro is a workaround for a problem, that detection method is using SDIO
 * interrupt pin, which could be used by SDIO driver at that same time.
 * It is safe to assume the we have the chip in a board file, that is defined
 * for a board with this chip.
 * */
#define COMBO_CHIP_ASSUME_DETECTED

static struct mtk_wcn_combo_gpio ariel_combo_gpio_data = {
	.conf = { .merge = true, .pcm = true },
	.pwr = {
		.pmu = MTK_GPIO_PIN(COMBO_PMU_EN),
		.rst = MTK_GPIO_PIN(COMBO_RST),
#ifndef COMBO_CHIP_ASSUME_DETECTED
		.det = MTK_GPIO_PIN(WIFI_EINT),
#endif
	},

	.eint.bgf = MTK_EINT_PIN(COMBO_BGF_EINT, EINTF_TRIGGER_LOW),

	.uart = {
		.rxd = MTK_GPIO_PIN(COMBO_URXD),
		.txd = MTK_GPIO_PIN(COMBO_UTXD),
	},

	.pcm = {
		.clk = MTK_GPIO_PIN(PCM_DAICLK),
		.out = MTK_GPIO_PIN(PCM_DAIPCMOUT),
		.in = MTK_GPIO_PIN(PCM_DAIPCMIN),
		.sync = MTK_GPIO_PIN(PCM_DAISYNC),
	},
};

struct mt_wake_event_map ariel_event_map[] = {
	{
		.domain = "EINT",
		.code = 9,
		.we = WEV_WIFI,
		.irq = EINT_IRQ(9),
	},
	{
		.domain = "EINT",
		.code = 8,
		.we = WEV_BT,
		.irq = EINT_IRQ(8),
	},
	{
		.domain = "EINT",
		.code = 7,
		.we = WEV_HALL,
		.irq = EINT_IRQ(7),
	},
	{
		.domain = "PMIC",
		.code = 20,
		.we = WEV_RTC,
		.irq = PMIC_IRQ(20),
	},
	{
		.domain = "PMIC",
		.code = 9,
		.we = WEV_PWR,
		.irq = PMIC_IRQ(9),
	},
	{
		.domain = "PMIC",
		.code = 14,
		.we = WEV_CHARGER,
		.irq = PMIC_IRQ(14),
	},
	{ /*empty*/ }
};

static void __init ariel_init_early(void)
{
	system_type = get_board_type();
	system_rev = get_board_rev();

	pr_info("board_type = 0x%x, board_rev = 0x%x", system_type, system_rev);

	mt_init_early();
}

struct mt_battery_charging_custom_data_wrapper mt_bat_charging_data_wrapper = {};

static void __init ariel_init(void)
{
	u8 audio_spk_cfg;
	u8 is_class_AB;

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	hw_cfg = get_hw_cfg();

	spm_set_wakeup_event_map(ariel_event_map);

	/* arch(3) phase:
	 * - initialize MTK pin manipulation API.
	 * - Create GPIO devices (gpio driver must have been registered at
	 *   post-core(2) phase)
	 * - Initialize and register EINTs.
	 * - configure SoC pins described in the table */
	mt_pinctrl_init(ariel_pin_cfg, ariel_eint_config);

	pm_power_off_prepare = mt_power_off_prepare;
	pm_power_off = mt_power_off;
	mt_charger_init();

	if (hw_cfg != NULL && hw_cfg->gpio_wifi_eint)
		ariel_wifi_sdio_data.irq.pin = hw_cfg->gpio_wifi_eint - 1;

	mtk_wifi_sdio_set_data(&ariel_wifi_sdio_data, CONFIG_MTK_WCN_CMB_SDIO_SLOT);

#ifdef CONFIG_MTK_INTERNAL_HDMI_SUPPORT
	mtk_hdmi_set_data(&ariel_hdmi_data);
#endif

	if (hw_cfg != NULL && hw_cfg->gpio_wifi_combo_bfg_eint)
		ariel_combo_gpio_data.eint.bgf.pin = hw_cfg->gpio_wifi_combo_bfg_eint - 1;

	mtk_combo_init(&ariel_combo_gpio_data);

	/* For PMIC */
	mt_pmic_init();

#ifdef CONFIG_MTK_KEYPAD
	mt_kpd_init();
#endif


	/* FIXME: REMOVE ME */

	i2c_devs_init();

	mt_bls_init(NULL);

	if (hw_cfg != NULL && hw_cfg->gpio_ext_spkamp_en)
		audio_custom_gpio_data.pmic_ext_spkamp_en = hw_cfg->gpio_ext_spkamp_en - 1;

	if (hw_cfg != NULL && hw_cfg->audio_spk_cfg)
		audio_spk_cfg  = hw_cfg->audio_spk_cfg - 1;
	else
		audio_spk_cfg = SPK_MONO_RIGHT;

	is_class_AB = 0;
	if (hw_cfg != NULL)
		is_class_AB = !!hw_cfg->is_audio_class_AB;

#ifdef CONFIG_MTK_SERIAL
	mt_ariel_serial_init();
#endif

	mt_clk_init();

	/* Do not init unneeded devices in the recovery safe mode */
	if (g_boot_mode != RECOVERY_BOOT) {

		mt_audio_init(&audio_custom_gpio_data,
			audio_spk_cfg,	/* 0=Stereo, 1=MonoLeft, 2=MonoRight */
			is_class_AB 	/*0=class-D, 1=class-AB */
		);

#ifdef CONFIG_INPUT_HALL_BU520
		hall_bu520_init();
#endif

		if (hw_cfg != NULL && hw_cfg->has_ov9724_cam) {
			mt_cam_init(ov9724_camera_sensor_conf,
					ARRAY_SIZE(ov9724_camera_sensor_conf));
		}

		if (hw_cfg != NULL && hw_cfg->has_s5k5e_cam) {
			mt_cam_init(s5k5e_camera_sensor_conf,
					ARRAY_SIZE(s5k5e_camera_sensor_conf));
		}

		if (system_type == BOARD_ID_ASTON) {
			if (system_rev == BOARD_REV_ASTON_PROTO)
				mt_cam_init(aston_proto_camera_sensor_conf,
						ARRAY_SIZE(aston_proto_camera_sensor_conf));
			else
				mt_cam_init(aston_evt_camera_sensor_conf,
						ARRAY_SIZE(aston_evt_camera_sensor_conf));
		} else if (system_type == BOARD_ID_ARIEL) {
			mt_cam_init(ariel_camera_sensor_conf, ARRAY_SIZE(ariel_camera_sensor_conf));
		}

	}

	set_usb_vbus_gpio(-1);

	mt_bat_charging_data_wrapper.params.offset = hw_cfg->battery_sensor_params[0];
	mt_bat_charging_data_wrapper.params.alpha = hw_cfg->battery_sensor_params[1];
	mt_bat_charging_data_wrapper.params.weight = hw_cfg->battery_sensor_params[2];

	mt_wmt_init();

}

static const char *const ariel_dt_match[] = { "mediatek,mt8135-ariel", NULL };

DT_MACHINE_START(MT8135, "MT8135")
	.smp            = smp_ops(mt65xx_smp_ops),
	.dt_compat      = ariel_dt_match,
	.map_io         = mt_map_io,
	.init_irq       = mt_dt_init_irq,
	.init_time      = mt8135_timer_init,
	.init_early     = ariel_init_early,
	.init_machine   = ariel_init,
	.fixup          = mt_fixup,
	.restart        = arm_machine_restart,
	.reserve        = mt_reserve,
MACHINE_END
