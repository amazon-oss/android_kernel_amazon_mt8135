#ifndef SENSOR_DEBUGFS_H
#define SENSOR_DEBUGFS_H

void main_sensor_debugfs_create(void);
void sub_sensor_debugfs_create(void);

enum main_sensor_debugfs_cmds{
SCRIPT_ALLOCATE,
SCRIPT_WRTIE,
SCRIPT_FREE,
};

struct i2c_settings_t {
	uint32_t addr;
	uint32_t value;
};

/* Register and script settings follow */
extern u16 main_sensor_address;
extern u16 main_sensor_value;
extern u32 main_sensor_cmd;

/* To be used for script reprogramming */
extern u32 main_sensor_size;
extern u32 main_sensor_idx;


/* Register and script settings follow */
extern u16 sub_sensor_address;
extern u16 sub_sensor_value;
extern  u32 sub_sensor_cmd;

/* To be used for script reprogramming */
extern u32 sub_sensor_size;
extern u32 sub_sensor_idx;


extern struct i2c_settings_t *main_sensor_dynamic_script;
extern struct i2c_settings_t *sub_sensor_dynamic_script;

#endif
