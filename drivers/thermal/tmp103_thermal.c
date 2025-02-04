/*
 * Copyright (C) 2013 Lab126, Inc.  All rights reserved.
 * Author: Akwasi Boateng <boatenga@lab126.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>

#include <linux/input/tmp103_temp_sensor.h>
#include <linux/platform_data/mtk_thermal.h>
#include <linux/thermal_framework.h>



#include <mach/mt_board.h>

#include "thermal_core.h"

#define DRIVER_NAME "tmp103-thermal"
#define THERMAL_NAME "tmp103"
#define BUF_SIZE 128
#define DMF 1000
#define MASK (0x0FFF)

static LIST_HEAD(thermal_sensor_list);
static DEFINE_MUTEX(therm_lock);

struct tmp103_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct mtk_thermal_platform_data *pdata;
};

#define PREFIX "thermalsensor:def"
static int tmp103_thermal_get_temp(struct thermal_zone_device *thermal,
				   unsigned long *t)
{
	struct thermal_dev *tdev;
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;
	long temp = 0;
	long tempv = 0;
	int alpha, offset, weight;

	if (!tzone || !pdata)
		return -EINVAL;


	list_for_each_entry(tdev, &thermal_sensor_list, node) {
		temp = tdev->dev_ops->get_temp(tdev);


		alpha = tdev->tdp->alpha;
		offset = tdev->tdp->offset;
		weight = tdev->tdp->weight;
		if (!tdev->off_temp)
			tdev->off_temp = temp - offset;
		else {
			tdev->off_temp = alpha * (temp - offset) +
				(DMF - alpha) * tdev->off_temp;
			tdev->off_temp /= DMF;
		}

		tempv += (weight * tdev->off_temp)/DMF;
	}


	*t = (unsigned long) tempv;

	return 0;
}
static int tmp103_thermal_get_mode(struct thermal_zone_device *thermal,
				   enum thermal_device_mode *mode)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;
	if (!pdata)
		return -EINVAL;

	mutex_lock(&therm_lock);
	*mode = pdata->mode;
	mutex_unlock(&therm_lock);
	return 0;
}
static int tmp103_thermal_set_mode(struct thermal_zone_device *thermal,
				   enum thermal_device_mode mode)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&therm_lock);
	pdata->mode = mode;
	if (mode == THERMAL_DEVICE_DISABLED) {
		tzone->tz->polling_delay = 0;
		thermal_zone_device_update(tzone->tz);
		mutex_unlock(&therm_lock);
		return 0;
	}
	schedule_work(&tzone->therm_work);
	mutex_unlock(&therm_lock);
	return 0;
}
static int tmp103_thermal_get_trip_type(struct thermal_zone_device *thermal,
					int trip,
					enum thermal_trip_type *type)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;
	return 0;
}
static int tmp103_thermal_get_trip_temp(struct thermal_zone_device *thermal,
					int trip,
					unsigned long *temp)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;
	return 0;
}
static int tmp103_thermal_set_trip_temp(struct thermal_zone_device *thermal,
					int trip,
					 unsigned long temp)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].temp = temp;
	return 0;
}
static int tmp103_thermal_get_crit_temp(struct thermal_zone_device *thermal,
					unsigned long *temp)
{
	int i;
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}
static int tmp103_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
					int trip,
					unsigned long *hyst)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;
	return 0;
}
static int tmp103_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
					int trip,
					unsigned long hyst)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}
static int tmp103_thermal_notify(struct thermal_zone_device *thermal,
				 int trip,
				 enum thermal_trip_type type)
{
	char data[20];
	char *envp[] = { data, NULL};
	snprintf(data, sizeof(data), "%s", "SHUTDOWN_WARNING");
	kobject_uevent_env(&thermal->device.kobj, KOBJ_CHANGE, envp);

	return 0;
}
static struct thermal_zone_device_ops tmp103_tz_dev_ops = {
	.get_temp = tmp103_thermal_get_temp,
	.get_mode = tmp103_thermal_get_mode,
	.set_mode = tmp103_thermal_set_mode,
	.get_trip_type = tmp103_thermal_get_trip_type,
	.get_trip_temp = tmp103_thermal_get_trip_temp,
	.set_trip_temp = tmp103_thermal_set_trip_temp,
	.get_crit_temp = tmp103_thermal_get_crit_temp,
	.get_trip_hyst = tmp103_thermal_get_trip_hyst,
	.set_trip_hyst = tmp103_thermal_set_trip_hyst,
	.notify = tmp103_thermal_notify,
};
static int params_show(struct device *dev,
		       struct device_attribute *devattr, char *buf)
{
	int o = 0;
	int a = 0;
	int w = 0;
	char pbufo[BUF_SIZE];
	char pbufa[BUF_SIZE];
	char pbufw[BUF_SIZE];
	int alpha, offset, weight;
	struct thermal_dev *tdev;

	o += sprintf(pbufo + o, "offsets ");
	a += sprintf(pbufa + a, "alphas ");
	w += sprintf(pbufw + w, "weights ");

	list_for_each_entry(tdev, &thermal_sensor_list, node) {
		alpha = tdev->tdp->alpha;
		offset = tdev->tdp->offset;
		weight = tdev->tdp->weight;

		o += sprintf(pbufo + o, "%d ", offset);
		a += sprintf(pbufa + a, "%d ", alpha);
		w += sprintf(pbufw + w, "%d ", weight);
	}
	return sprintf(buf, "%s\n%s\n%s\n", pbufo, pbufa, pbufw);
}
static ssize_t trips_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->trips);
}
static ssize_t trips_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int trips = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	if (sscanf(buf, "%d\n", &trips) != 1)
		return -EINVAL;
	if (trips < 0)
		return -EINVAL;

	pdata->num_trips = trips;
	thermal->trips = pdata->num_trips;
	return count;
}
static ssize_t polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}
static ssize_t polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &polling_delay) != 1)
		return -EINVAL;
	if (polling_delay < 0)
		return -EINVAL;

	pdata->polling_delay = polling_delay;
	thermal->polling_delay = pdata->polling_delay;
	thermal_zone_device_update(thermal);
	return count;
}

static DEVICE_ATTR(trips, S_IRUGO | S_IWUSR, trips_show, trips_store);
static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, polling_show, polling_store);
static DEVICE_ATTR(params, S_IRUGO, params_show, NULL);

static int tmp103_create_sysfs(struct tmp103_thermal_zone *tzone)
{
	int ret = 0;
	ret = device_create_file(&tzone->tz->device, &dev_attr_params);
	if (ret)
		pr_err("%s Failed to create polling attr\n", __func__);
	ret = device_create_file(&tzone->tz->device, &dev_attr_polling);
	if (ret)
		pr_err("%s Failed to create polling attr\n", __func__);
	ret = device_create_file(&tzone->tz->device, &dev_attr_trips);
	if (ret)
		pr_err("%s Failed to create trips attr\n", __func__);
	return ret;
}
static void tmp103_thermal_work(struct work_struct *work)
{
	struct tmp103_thermal_zone *tzone;
	struct mtk_thermal_platform_data *pdata;

	mutex_lock(&therm_lock);
	tzone = container_of(work, struct tmp103_thermal_zone, therm_work);
	if (!tzone)
		return;
	pdata = tzone->pdata;
	if (!pdata)
		return;
	if (pdata->mode == THERMAL_DEVICE_ENABLED)
		thermal_zone_device_update(tzone->tz);
	mutex_unlock(&therm_lock);
}
static int tmp103_thermal_probe(struct platform_device *pdev)
{
	int ret;
	struct tmp103_thermal_zone *tzone;
	struct mtk_thermal_platform_data *pdata = NULL;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	tzone = devm_kzalloc(&pdev->dev, sizeof(*tzone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;
	memset(tzone, 0, sizeof(*tzone));

	tzone->pdata = pdata;
	tzone->tz = thermal_zone_device_register(THERMAL_NAME,
						 THERMAL_MAX_TRIPS,
						 MASK,
						 tzone,
						 &tmp103_tz_dev_ops,
						 &pdata->tzp,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s Failed to register thermal zone device\n", __func__);
		kfree(tzone);
		return -EINVAL;
	}

	tzone->tz->trips = pdata->num_trips;
	ret = tmp103_create_sysfs(tzone);
	INIT_WORK(&tzone->therm_work, tmp103_thermal_work);
	platform_set_drvdata(pdev, tzone);
	pdata->mode = THERMAL_DEVICE_ENABLED;
	return ret;
}
static int tmp103_thermal_remove(struct platform_device *pdev)
{
	struct tmp103_thermal_zone *tzone = platform_get_drvdata(pdev);
	if (tzone) {
		cancel_work_sync(&tzone->therm_work);
		if (tzone->tz)
			thermal_zone_device_unregister(tzone->tz);
		kfree(tzone);
	}
	return 0;
}
int thermal_dev_register(struct thermal_dev *tdev)
{
	if (unlikely(IS_ERR_OR_NULL(tdev))) {
		pr_err("%s: NULL sensor thermal device\n", __func__);
		return -ENODEV;
	}
	if (!tdev->dev_ops->get_temp) {
		pr_err("%s: Error getting get_temp()\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&therm_lock);
	list_add_tail(&tdev->node, &thermal_sensor_list);
	mutex_unlock(&therm_lock);
	return 0;
}
EXPORT_SYMBOL(thermal_dev_register);

static struct platform_driver tmp103_thermal_zone_driver = {
	.probe = tmp103_thermal_probe,
	.remove = tmp103_thermal_remove,
	.suspend = NULL,
	.resume = NULL,
	.shutdown   = NULL,
	.driver     = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init tmp103_thermal_init(void)
{
	return platform_driver_register(&tmp103_thermal_zone_driver);
}
static void __exit tmp103_thermal_exit(void)
{
	platform_driver_unregister(&tmp103_thermal_zone_driver);
}

late_initcall(tmp103_thermal_init);
module_exit(tmp103_thermal_exit);

MODULE_DESCRIPTION("TMP103 pcb virtual sensor thermal zone driver");
MODULE_AUTHOR("Akwasi Boateng <boatenga@amazon.com>");
MODULE_LICENSE("GPL");
