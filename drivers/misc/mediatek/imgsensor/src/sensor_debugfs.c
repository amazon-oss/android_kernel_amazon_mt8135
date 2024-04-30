#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include "sensor_debugfs.h"

struct i2c_settings_t *main_sensor_dynamic_script = NULL;
struct i2c_settings_t *sub_sensor_dynamic_script = NULL;

/* Register and script settings follow */
u16 main_sensor_address = 0;
u16 main_sensor_value = 0;
u32 main_sensor_cmd = 0;

/* To be used for script reprogramming */
u32 main_sensor_size=0;
u32 main_sensor_idx;


/* Register and script settings follow */
u16 sub_sensor_address = 0;
u16 sub_sensor_value = 0;
u32 sub_sensor_cmd = 0;

/* To be used for script reprogramming */
u32 sub_sensor_size;
u32 sub_sensor_idx;

static int main_sensor_sensor_cmd_set(void *data, u64 val)
{

	printk("%s \n", __func__);

	switch(val)
	{
		case SCRIPT_ALLOCATE:
			pr_err("KK: Allocating s5k5e2y dynamic script of size %d \n", main_sensor_size);
			kfree(main_sensor_dynamic_script); /* Free old script */
			main_sensor_dynamic_script = kzalloc((sizeof(struct i2c_settings_t) * main_sensor_size ), GFP_KERNEL);
			if(main_sensor_dynamic_script == NULL)
				printk("KK: Unable to allocate memeory for Dynamic Script \n");


		        break;

		case SCRIPT_WRTIE:
			pr_err("KK: Writing index[%d] with Address[%x] and Data[%x]", main_sensor_idx, main_sensor_address, main_sensor_value);
			if(main_sensor_dynamic_script)
			{
				main_sensor_dynamic_script[main_sensor_idx].addr  = main_sensor_address;
				main_sensor_dynamic_script[main_sensor_idx].value = main_sensor_value;

			}
			break;

		case SCRIPT_FREE:
			kfree(main_sensor_dynamic_script); /* Free old script */
			main_sensor_size = 0;
			break;

	}

return 0;
}

static int main_sensor_sensor_cmd_get(void *data, u64 *val)
{
	printk("%s \n", __func__);
        *val = *(u32 *)data;
        return 0;
}

static int sub_sensor_sensor_cmd_set(void *data, u64 val)
{

	printk("%s \n", __func__);

	switch(val)
	{
		case SCRIPT_ALLOCATE:
			pr_err("KK: Allocating s5k5e2y dynamic script of size %d \n", sub_sensor_size);
			kfree(sub_sensor_dynamic_script); /* Free old script */
			sub_sensor_dynamic_script = kzalloc((sizeof(struct i2c_settings_t) * sub_sensor_size ), GFP_KERNEL);
			if(sub_sensor_dynamic_script == NULL)
				printk("KK: Unable to allocate memeory for Dynamic Script \n");


		        break;

		case SCRIPT_WRTIE:
			pr_err("KK: Writing index[%d] with Address[%x] and Data[%x]", sub_sensor_idx, sub_sensor_address, sub_sensor_value);
			if(sub_sensor_dynamic_script)
			{
				sub_sensor_dynamic_script[sub_sensor_idx].addr  = sub_sensor_address;
				sub_sensor_dynamic_script[sub_sensor_idx].value = sub_sensor_value;

			}
			break;

		case SCRIPT_FREE:
			kfree(sub_sensor_dynamic_script); /* Free old script */
			sub_sensor_size = 0;
			break;

	}

return 0;
}

static int sub_sensor_sensor_cmd_get(void *data, u64 *val)
{
	printk("%s \n", __func__);
        *val = *(u32 *)data;
        return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(main_sensor_sensor_cmd_fops, main_sensor_sensor_cmd_get, main_sensor_sensor_cmd_set, "%llu\n" );
DEFINE_SIMPLE_ATTRIBUTE(sub_sensor_sensor_cmd_fops, sub_sensor_sensor_cmd_get, sub_sensor_sensor_cmd_set, "%llu\n" );

struct dentry *main_sensor_mydebugfs_create_cmd32(const char *name, mode_t mode,
                                 struct dentry *parent, u32 *value)
{
        /* if there are no write bits set, make read only */
        if (!(mode & S_IWUGO))
                return debugfs_create_file(name, mode, parent, value, &main_sensor_sensor_cmd_fops);
        /* if there are no read bits set, make write only */
        if (!(mode & S_IRUGO))
                return debugfs_create_file(name, mode, parent, value, &main_sensor_sensor_cmd_fops);

        return debugfs_create_file(name, mode, parent, value, &main_sensor_sensor_cmd_fops);
}


struct dentry *sub_sensor_mydebugfs_create_cmd32(const char *name, mode_t mode,
                                 struct dentry *parent, u32 *value)
{
        /* if there are no write bits set, make read only */
        if (!(mode & S_IWUGO))
                return debugfs_create_file(name, mode, parent, value, &sub_sensor_sensor_cmd_fops);
        /* if there are no read bits set, make write only */
        if (!(mode & S_IRUGO))
                return debugfs_create_file(name, mode, parent, value, &sub_sensor_sensor_cmd_fops);

        return debugfs_create_file(name, mode, parent, value, &sub_sensor_sensor_cmd_fops);
}

void main_sensor_debugfs_create(void)
{
	struct dentry * main_sensor_tune;

        main_sensor_tune = debugfs_create_dir("main_sensor_ctrl", NULL);
        if (!main_sensor_tune) {

                main_sensor_tune = NULL;
                printk("Unable to create debugfs directory \n");
                return;
        }

        else {
		main_sensor_mydebugfs_create_cmd32("cmd", 0600, main_sensor_tune, &main_sensor_cmd);
		debugfs_create_x16("address", 0600, main_sensor_tune, &main_sensor_address);
		debugfs_create_x16("value", 0600, main_sensor_tune, &main_sensor_value);
		debugfs_create_u32("size", 0600, main_sensor_tune, &main_sensor_size);
		debugfs_create_u32("index", 0600, main_sensor_tune, &main_sensor_idx);

	}
}

void sub_sensor_debugfs_create(void)
{
	struct dentry * sub_sensor_tune;

        sub_sensor_tune = debugfs_create_dir("sub_sensor_ctrl", NULL);
        if (!sub_sensor_tune) {

                sub_sensor_tune = NULL;
                printk("Unable to create debugfs directory \n");
                return;
        }

        else {
		sub_sensor_mydebugfs_create_cmd32("cmd", 0600, sub_sensor_tune, &sub_sensor_cmd);
		debugfs_create_x16("address", 0600, sub_sensor_tune, &sub_sensor_address);
		debugfs_create_x16("value", 0600, sub_sensor_tune, &sub_sensor_value);
		debugfs_create_u32("size", 0600, sub_sensor_tune, &sub_sensor_size);
		debugfs_create_u32("index", 0600, sub_sensor_tune, &sub_sensor_idx);

	}
}
