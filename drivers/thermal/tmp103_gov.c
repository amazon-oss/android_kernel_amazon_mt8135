/*
 *  tmp103_gov.c - A simple thermal throttling governor
 *
 *  Copyright (C) 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved
 *  Author: Akwasi Boateng <boatenga@amazon.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 *
 */

#include <linux/thermal.h>
#include <linux/module.h>
#include "thermal_core.h"

/**
 * tmp103_throttle
 * @tz - thermal_zone_device
 * @trip - the trip point
 *
 */
static int tmp103_throttle(struct thermal_zone_device *tz, int trip)
{
	long trip_temp, temp;
	struct thermal_instance *tz_instance, *cd_instance;
	struct thermal_cooling_device *cdev;
	unsigned long target = 0;
	char data[3][25];
	char *envp[] = { data[0], data[1], data[2], NULL };
	unsigned long max_state;
	unsigned long cur_state;

	if (trip)
		return 0;

	mutex_lock(&tz->lock);
	list_for_each_entry(tz_instance, &tz->thermal_instances, tz_node) {
		if (tz_instance->trip != trip)
			continue;


		cdev = tz_instance->cdev;
		cdev->ops->get_max_state(cdev, &max_state);

		mutex_lock(&cdev->lock);
		list_for_each_entry(cd_instance, &cdev->thermal_instances, cdev_node) {
			if (cd_instance->trip >= tz->trips)
				continue;

			if (trip == THERMAL_TRIPS_NONE)
				trip_temp = tz->forced_passive;
			else
				tz->ops->get_trip_temp(tz, cd_instance->trip, &trip_temp);

			if (tz->temperature >= trip_temp)
				target = cd_instance->trip + 1;

			cd_instance->target = THERMAL_NO_TARGET;
		}

		target = (target > max_state) ? max_state : target;

		cdev->ops->get_cur_state(cdev, &cur_state);
		if (cur_state != target) {
			tz_instance->target = target;
			cdev->updated = false;
		}
		mutex_unlock(&cdev->lock);

		if (cur_state != target) {
			thermal_cdev_update(cdev);

			if (target)
				tz->ops->get_trip_temp(tz, target - 1, &temp);
			else
				tz->ops->get_trip_temp(tz, target, &temp);

			if (target)
				pr_notice("VS throttling, cur_temp=%d, trip_temp=%ld, target=%lu\n",
								tz->temperature, temp, target);
			else
				pr_notice("VS unthrottling, cur_temp=%d, trip_temp=%ld\n",
								tz->temperature, temp);

			snprintf(data[0], sizeof(data[0]), "TRIP=%d", trip);
			snprintf(data[1], sizeof(data[1]), "THERMAL_STATE=%ld", target);
			/*
			 * PhoneWindowManagerMetricsHelper.java may need to filter out
			 * all but one cooling device type.
			 */
			snprintf(data[2], sizeof(data[2]), "CDEV_TYPE=%s", cdev->type);
			kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);
		}
	}
	mutex_unlock(&tz->lock);
	return 0;
}

static struct thermal_governor thermal_gov_tmp103 = {
	.name = "tmp103",
	.throttle = tmp103_throttle,
};

static int __init thermal_gov_tmp103_init(void)
{
	return thermal_register_governor(&thermal_gov_tmp103);
}

static void __exit thermal_gov_tmp103_exit(void)
{
	thermal_unregister_governor(&thermal_gov_tmp103);
}

fs_initcall(thermal_gov_tmp103_init);
module_exit(thermal_gov_tmp103_exit);

MODULE_AUTHOR("Akwasi Boateng");
MODULE_DESCRIPTION("A simple level throttling thermal governor");
MODULE_LICENSE("GPL");
