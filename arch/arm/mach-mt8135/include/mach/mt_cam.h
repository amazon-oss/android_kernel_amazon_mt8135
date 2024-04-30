#ifndef _MT_CAM_H
#define _MT_CAM_H

#define ID_ALEXANDRIA		0x0022
#define ID_MEMPHIS			0x0021
#define ID_THEBES			0x0020
#define ID_STRINGRAY		0x0013
#define ID_ASTON			0x0012
#define ID_ARIEL			0x000F

struct mt_clk_pin;

struct mt_camera_sensor_config {
	char *name;
	char *mclk_name;
	char *auxclk_name;
	u16 rst_pin;
	u16 pdn_pin;
	bool rst_on:1; /* active level */
	bool pdn_on:1; /* active level */
	bool disabled:1; /* sensor is disabled */
	int vcam_a; /* sensor analog power */
	int vcam_d; /* sensor digital power */
	int vcam_io; /* i2c bus power, optional */
};

void mt_cam_init(struct mt_camera_sensor_config *config, int sensor_num);

#endif
