#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/aee.h>
#include <linux/xlog.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/reboot.h>

#include <mach/mt_irq.h>
#include "mach/mtk_thermal_monitor.h"
#include <mach/system.h>

#include "mach/mt_typedefs.h"
#include "mach/mt_thermal.h"
#include "mach/mt_cpufreq.h"
#include "mach/mt_gpufreq.h"

#include <linux/platform_data/mtk_thermal.h>
#include <drivers/thermal/thermal_core.h>

static int Num_of_GPU_OPP;

static struct mt_cpu_power_info *mtk_cpu_power;
static struct mt_gpufreq_power_info *mtk_gpu_power;

#define CONFIG_SUPPORT_MET_CPU_ADAPTIVE 1
#if CONFIG_SUPPORT_MET_CPU_ADAPTIVE
/* MET */
#include <linux/export.h>
#include <linux/met_drv.h>

static char header[] =
	"met-info [000] 0.0: ms_ud_sys_header: CPU_Temp,"
	"TS1_temp,TS2_temp,TS3_temp,TS4_temp,d,d,d,d\n"
	"met-info [000] 0.0: ms_ud_sys_header: P_adaptive,"
	"CPU_power,GPU_power,d,d\n";

static const char help[] = "  --cpu_adaptive                              monitor cpu_adaptive\n";

static int sample_print_help(char *buf, int len)
{
	return snprintf(buf, PAGE_SIZE, help);
}

static int sample_print_header(char *buf, int len)
{
	return snprintf(buf, PAGE_SIZE, header);
}

unsigned int met_cpu_adaptive_dbg = 0;
static void sample_start(void)
{
	met_cpu_adaptive_dbg = 1;
	return;
}

static void sample_stop(void)
{
	met_cpu_adaptive_dbg = 0;
	return;
}

struct metdevice met_cpu_adaptive = {
	.name = "cpu_adaptive",
	.owner = THIS_MODULE,
	.type = MET_TYPE_BUS,
	.start = sample_start,
	.stop = sample_stop,
	.print_help = sample_print_help,
	.print_header = sample_print_header,
};
EXPORT_SYMBOL(met_cpu_adaptive);
#endif

/* #define TARGET_TJ (65700) */
#define TARGET_HIGH_RANGE (2000)
#define TARGET_LOW_RANGE (1000)
static int TARGET_TJ = 95000;
static int TARGET_TJ_HIGH = 96000;
static int TARGET_TJ_LOW = 94000;
/* #define PACKAGE_THETA_JA (10) */
static int PACKAGE_THETA_JA = 10;
/* DVFS-4 */
/* #define MINIMUM_CPU_POWER (582) */
/* #define MAXIMUM_CPU_POWER (4124) */
/* DVFS-7 */
#define MINIMUM_CPU_POWER (594)
static int MAXIMUM_CPU_POWER = 4051;
static int MINIMUM_GPU_POWER = 500;
static int MAXIMUM_GPU_POWER = 1449;
static int MINIMUM_TOTAL_POWER = 1094;
static int MAXIMUM_TOTAL_POWER = 5500;
/* DVFS-4 */
/* #define FIRST_STEP_TOTAL_POWER_BUDGET (1550) */
/* DVFS-7 */
/* #define FIRST_STEP_TOTAL_POWER_BUDGET (1450) */
/* #define FIRST_STEP_TOTAL_POWER_BUDGET (1763) */
static int FIRST_STEP_TOTAL_POWER_BUDGET = 4600;

/* 1. MINIMUM_BUDGET_CHANGE = 0 ==> thermal equilibrium maybe at higher than TARGET_TJ_HIGH */
/* 2. Set MINIMUM_BUDGET_CHANGE > 0 if to keep Tj at TARGET_TJ */
#define MINIMUM_BUDGET_CHANGE (50)	/* mW */

static int NOTIFY_HOTPLUG;

static int P_adaptive(int total_power, unsigned int gpu_loading)
{
	static int cpu_power = 0, gpu_power;
	static int last_cpu_power = 0, last_gpu_power;

	if (total_power == 0) {
		if (!NOTIFY_HOTPLUG)
			mt_cpufreq_thermal_protect(MAXIMUM_CPU_POWER);
		else
			thermal_budget_notify(MAXIMUM_CPU_POWER);

		mt_gpufreq_thermal_protect(MAXIMUM_GPU_POWER);
#if CONFIG_SUPPORT_MET_CPU_ADAPTIVE
		if (met_cpu_adaptive_dbg)
			trace_printk("%d,%d\n", 5000, 5000);
#endif
		return 0;
	}

	last_cpu_power = cpu_power;
	last_gpu_power = gpu_power;

	if (Num_of_GPU_OPP >= 2) {
		if (gpu_loading < 50)
			gpu_power =
			    (((gpu_loading * gpu_loading) + (500 * gpu_loading) +
			      2000) * 105) / 5000;
		else
			gpu_power =
			    (((gpu_loading * gpu_loading) + (500 * gpu_loading) +
			      3000) * 115) / 5000;
	} else {
		gpu_power = MINIMUM_GPU_POWER;
	}

	cpu_power = total_power - gpu_power;
	if (cpu_power < MINIMUM_CPU_POWER) {
		cpu_power = MINIMUM_CPU_POWER;
		gpu_power = total_power - MINIMUM_CPU_POWER;

		if (gpu_power != last_gpu_power)
			mt_gpufreq_thermal_protect(gpu_power);
	} else if (cpu_power > MAXIMUM_CPU_POWER)
		cpu_power = MAXIMUM_CPU_POWER;

	if (cpu_power != last_cpu_power) {
		if (!NOTIFY_HOTPLUG)
			mt_cpufreq_thermal_protect(cpu_power);
		else
			thermal_budget_notify(cpu_power);
	}

#if CONFIG_SUPPORT_MET_CPU_ADAPTIVE
	if (met_cpu_adaptive_dbg)
		trace_printk("%d,%d\n", cpu_power, gpu_power);
#endif

	return 0;
}

static int _adaptive_power(struct thermal_zone_device *tz, unsigned long state,
			   unsigned long prev_temp,
			   unsigned long curr_temp,
			   unsigned int gpu_loading)
{
	static int triggered = 0, total_power;
	static int is_raising;
	char data[25];
	char *envp[] = {data, NULL};


	int delta_power = 0;

	if (state) {
		/* Check if it is triggered */
		if (!triggered) {
			if (curr_temp < TARGET_TJ)
				return 0;
			else {
				triggered = 1;
				total_power = FIRST_STEP_TOTAL_POWER_BUDGET;

				pr_notice("CPU adaptive cooler: throttling starts, temp=%lu\n", curr_temp);

				snprintf(data, sizeof(data), "THERMAL_STATE=%lu", curr_temp);
				kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);

				return P_adaptive(total_power, gpu_loading);
			}
		}

		/* Adjust total power budget if necessary */
		if ((curr_temp > TARGET_TJ_HIGH) && (curr_temp >= prev_temp)) {

			if (!is_raising) {
				pr_notice("CPU adaptive cooler: temp is raising, prev=%lu, curr=%lu\n",
										prev_temp, curr_temp);
				snprintf(data, sizeof(data), "THERMAL_STATE=%lu", curr_temp);
				kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);
				is_raising = 1;
			}

			delta_power = (curr_temp - prev_temp) / PACKAGE_THETA_JA;
			if (prev_temp > TARGET_TJ_HIGH) {
				delta_power =
				    (delta_power >
				     MINIMUM_BUDGET_CHANGE) ? delta_power : MINIMUM_BUDGET_CHANGE;
			}
			total_power -= delta_power;
			total_power =
				(total_power > MINIMUM_TOTAL_POWER) ? total_power : MINIMUM_TOTAL_POWER;
		}

		if ((curr_temp < TARGET_TJ_LOW) && (curr_temp <= prev_temp)) {
			if (is_raising) {
				pr_notice("CPU adaptive cooler: temp is declining, prev=%lu, curr=%lu\n",
										prev_temp, curr_temp);
				snprintf(data, sizeof(data), "THERMAL_STATE=%lu", curr_temp);
				kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);
				is_raising = 0;
			}

			delta_power = (prev_temp - curr_temp) / PACKAGE_THETA_JA;
			if (prev_temp < TARGET_TJ_LOW) {
				delta_power =
				    (delta_power >
				     MINIMUM_BUDGET_CHANGE) ? delta_power : MINIMUM_BUDGET_CHANGE;
			}
			total_power += delta_power;
			total_power =
			    (total_power < MAXIMUM_TOTAL_POWER) ? total_power : MAXIMUM_TOTAL_POWER;
		}

		return P_adaptive(total_power, gpu_loading);
	} else {
		if (triggered) {
			pr_notice("CPU adaptive cooler: unthrottling starts, temp=%lu\n", curr_temp);
			snprintf(data, sizeof(data), "THERMAL_STATE=%lu", curr_temp);
			kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);
			triggered = 0;
			return P_adaptive(0, 0);
		}
	}

	return 0;
}

int mtk_cpufreq_register(struct mt_cpu_power_info *freqs, int num)
{
	int i = 0;

	mtk_cpu_power = kzalloc((num) * sizeof(struct mt_cpu_power_info), GFP_KERNEL);
	if (mtk_cpu_power == NULL)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		mtk_cpu_power[i].cpufreq_khz = freqs[i].cpufreq_khz;
		mtk_cpu_power[i].cpufreq_khz_ca7 = freqs[i].cpufreq_khz_ca7;
		mtk_cpu_power[i].cpufreq_khz_ca15 = freqs[i].cpufreq_khz_ca15;
		mtk_cpu_power[i].cpufreq_ncpu = freqs[i].cpufreq_ncpu;
		mtk_cpu_power[i].cpufreq_ncpu_ca7 = freqs[i].cpufreq_ncpu_ca7;
		mtk_cpu_power[i].cpufreq_ncpu_ca15 = freqs[i].cpufreq_ncpu_ca15;
		mtk_cpu_power[i].cpufreq_power = freqs[i].cpufreq_power;
		mtk_cpu_power[i].cpufreq_power_ca7 = freqs[i].cpufreq_power_ca7;
		mtk_cpu_power[i].cpufreq_power_ca15 = freqs[i].cpufreq_power_ca15;
	}

	MAXIMUM_CPU_POWER = mtk_cpu_power[0].cpufreq_power;
	MAXIMUM_TOTAL_POWER = MAXIMUM_CPU_POWER + MAXIMUM_GPU_POWER;
	return 0;
}
EXPORT_SYMBOL(mtk_cpufreq_register);


int mtk_gpufreq_register(struct mt_gpufreq_power_info *freqs, int num)
{
	int i = 0;

	mtk_gpu_power = kzalloc((num) * sizeof(struct mt_gpufreq_power_info), GFP_KERNEL);
	if (mtk_gpu_power == NULL)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		mtk_gpu_power[i].gpufreq_khz = freqs[i].gpufreq_khz;
		mtk_gpu_power[i].gpufreq_power = freqs[i].gpufreq_power;
	}

	Num_of_GPU_OPP = num;

	MAXIMUM_GPU_POWER = mtk_gpu_power[0].gpufreq_power;
	MINIMUM_GPU_POWER = mtk_gpu_power[Num_of_GPU_OPP - 1].gpufreq_power;
	MAXIMUM_TOTAL_POWER = MAXIMUM_CPU_POWER + MAXIMUM_GPU_POWER;

	return 0;
}
EXPORT_SYMBOL(mtk_gpufreq_register);


static int mtk_cpu_adaptive_get_max_state(struct thermal_cooling_device *cdev,
					  unsigned long *state)
{
	struct mtk_cooler_platform_data *pdata = cdev->devdata;
	*state = pdata->max_state;
	return 0;
}

static int mtk_cpu_adaptive_get_cur_state(struct thermal_cooling_device *cdev,
					  unsigned long *state)
{
	struct mtk_cooler_platform_data *pdata = cdev->devdata;
	*state = pdata->state;
	return 0;
}

static int mtk_cpu_adaptive_set_cur_state(struct thermal_cooling_device *cdev,
					  unsigned long state)
{
	struct mtk_cooler_platform_data *pdata = cdev->devdata;
	struct thermal_zone_device *tz;
	struct thermal_instance *instance;
	unsigned long prev_temp, curr_temp;

	list_for_each_entry(instance, &cdev->thermal_instances, cdev_node) {
		if (instance->target == state)
			break;
	}

	if (!instance)
		return 0;

	tz = instance->tz;

	prev_temp = tz->last_temperature;
	curr_temp = tz->temperature;
	pdata->state = state;

	_adaptive_power(tz, state, prev_temp, curr_temp, mt_gpufreq_get_cur_load());
	return 0;
}

static struct thermal_cooling_device_ops mtk_cpu_adaptive_cooler_ops = {
	.get_max_state = mtk_cpu_adaptive_get_max_state,
	.get_cur_state = mtk_cpu_adaptive_get_cur_state,
	.set_cur_state = mtk_cpu_adaptive_set_cur_state,
};

static int mtktscpu_show_dtm_setting(struct seq_file *s, void *v)
{
	seq_printf(s, "first_step = %d\n", FIRST_STEP_TOTAL_POWER_BUDGET);
	seq_printf(s, "theta = %d\n", PACKAGE_THETA_JA);
	seq_printf(s, "notify hotplug = %d\n", NOTIFY_HOTPLUG);
	return 0;
}

static int mtktscpu_open_dtm_setting(struct inode *inode, struct file *file)
{
	return single_open(file, mtktscpu_show_dtm_setting, NULL);
}

static ssize_t mtktscpu_write_dtm_setting(struct file *file, const char *buffer,
						size_t count, loff_t *data)
{
	char desc[32];
	char arg_name[32] = { 0 };
	int arg_val = 0;
	int len = 0;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	if (2 == sscanf(desc, "%s %d", arg_name, &arg_val)) {
		if (0 == strncmp(arg_name, "first_step", 10)) {
			FIRST_STEP_TOTAL_POWER_BUDGET = arg_val;
			pr_debug("[mtktscpu_write_dtm_setting] first_step=%d\n",
				FIRST_STEP_TOTAL_POWER_BUDGET);
		} else if (0 == strncmp(arg_name, "theta", 5)) {
			PACKAGE_THETA_JA = arg_val;
			pr_debug("[mtktscpu_write_dtm_setting] theta=%d\n",
				PACKAGE_THETA_JA);
		} else if (0 == strncmp(arg_name, "hotplug", 7)) {
			NOTIFY_HOTPLUG = arg_val;
			pr_debug("[mtktscpu_write_dtm_setting] notify_hotplug=%d\n",
				NOTIFY_HOTPLUG);
		}
		return count;
	} else {
		pr_debug("[mtktscpu_write_dtm_setting] bad argument\n");
		return -EINVAL;
	}
}

static const struct file_operations mtktscpu_dtm_setting_fops = {
	.owner = THIS_MODULE,
	.write = mtktscpu_write_dtm_setting,
	.open = mtktscpu_open_dtm_setting,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};



static int mtk_cpu_adaptive_cooler_probe(struct platform_device *pdev)
{
	struct mtk_cooler_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct proc_dir_entry *mtktscpu_dir, *entry;

	if (!pdata)
		return -EINVAL;

	pdata->cdev = thermal_cooling_device_register(pdata->type,
						pdata,
						&mtk_cpu_adaptive_cooler_ops);
	if (!pdata->cdev) {
		pr_err("%s Failed to create cooling device\n", __func__);
		return -EINVAL;
	}

	mtktscpu_dir = proc_mkdir("mtktscpu", NULL);
	if (!mtktscpu_dir) {
		pr_err("mtk_thermal_probe mkdir /proc/driver/thermal failed\n");
	} else {
		entry = proc_create("mtktscpu_dtm_setting", S_IRUGO | S_IWUSR,
					mtktscpu_dir, &mtktscpu_dtm_setting_fops);
		if (!entry)
			pr_err("[mtktscpu_init]: create clatm_setting failed\n");
	}

	return 0;
}

static int mtk_cpu_adaptive_cooler_remove(struct platform_device *pdev)
{
	struct mtk_cooler_platform_data *pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -EINVAL;
	thermal_cooling_device_unregister(pdata->cdev);
	return 0;
}

static struct platform_driver mtk_cpu_adaptive_cooler_driver = {
	.probe = mtk_cpu_adaptive_cooler_probe,
	.remove = mtk_cpu_adaptive_cooler_remove,
	.driver = {
		.name = "mtk_cpu_adaptive_cooler",
	},
};

static int __init mtk_cpu_adaptive_cooler_init(void)
{
	return platform_driver_register(&mtk_cpu_adaptive_cooler_driver);
}

static void __exit mtk_cpu_adaptive_cooler_exit(void)
{
	return platform_driver_unregister(&mtk_cpu_adaptive_cooler_driver);
}

module_init(mtk_cpu_adaptive_cooler_init);
module_exit(mtk_cpu_adaptive_cooler_exit);

