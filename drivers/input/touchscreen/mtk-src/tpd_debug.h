#ifndef TPD_DEBUG_H
#define TPD_DEBUG_H
#ifdef TPD_DEBUG_CODE
#include<linux/i2c.h>

void tpd_debug_no_response(struct i2c_client *);
extern int tpd_debug_nr;

#define TPD_DEBUG_CHECK_NO_RESPONSE \
	if (tpd_debug_nr) { \
		wait_event_interruptible_timeout(waiter, tpd_flag != 0, HZ/10); \
		if (tpd_flag == 0) { \
			tpd_debug_no_response(i2c_client); \
			continue; \
		} \
	} else


void tpd_debug_set_time(void);
extern int tpd_debug_time;
extern long tpd_last_2_int_time[2];
extern long tpd_last_down_time;
extern int tpd_start_profiling;
extern int tpd_down_status;

#define TPD_DEBUG_PRINT_INT                                                 \
	do {                                                                    \
		if (tpd_debug_time) {                                                \
			pr_info("tp_int\n");                                             \
		}                                                                   \
	} while (0)

#define TPD_DEBUG_PRINT_UP                                                  \
	do {                                                                    \
		if (pending == 0 && tpd_debug_time) {                                  \
			tpd_down_status = 0;                                              \
			pr_info("up on %ld ms (+%ld ms)\n",                              \
			(tpd_last_2_int_time[1] - tpd_last_down_time) / 1000,        \
			(tpd_last_2_int_time[1] - tpd_last_2_int_time[0]) / 1000);   \
		}                                                                   \
	} while (0)

#define TPD_DEBUG_PRINT_DOWN                                                    \
	do {                                                                        \
		if (tpd_debug_time) {                                                    \
			if (tpd_down_status == 0)				\
				pr_info("down on 0 ms\n");                    \
			else						\
				pr_info("move on %ld ms (+%ld ms)\n",                           \
			(tpd_last_2_int_time[1] - tpd_last_down_time) / 1000,       \
			(tpd_last_2_int_time[1] - tpd_last_2_int_time[0]) / 1000);  \
			tpd_down_status = 1;                                                  \
		}                                                                       \
	} while (0)

#define TPD_DEBUG_SET_TIME   do { tpd_debug_set_time(); } while (0);

extern int tpd_em_log;
extern int tpd_em_log_to_fs;
extern int tpd_type_cap;

void tpd_em_log_output(int raw_x, int raw_y, int cal_x, int cal_y, int p, int down);
void tpd_em_log_store(int raw_x, int raw_y, int cal_x, int cal_y, int p, int down);
void tpd_em_log_release(void);

#define TPD_TYPE_RAW_DATA   2
#define TPD_TYPE_INT_DOWN   3
#define TPD_TYPE_INT_UP     4
#define TPD_TYPE_TIMER      5
#define TPD_TYPE_REJECT1		6
#define TPD_TYPE_REJECT2		7
#define TPD_TYPE_FIST_LATENCY		8

#define TPD_EM_PRINT(raw_x, raw_y, cal_x, cal_y, p, down)                           \
	do {                                                                            \
		if (tpd_em_log) {                                                           \
			if (!tpd_em_log_to_fs) {                                                \
				tpd_em_log_output(raw_x, raw_y, cal_x, cal_y, p, down);             \
			} else {                                                                \
				tpd_em_log_store(raw_x, raw_y, cal_x, cal_y, p, down);              \
				tpd_em_log_output(raw_x, raw_y, cal_x, cal_y, p, down);                \
			}                                                                       \
		if (down == 1)                                                          \
			tpd_down_status = 1;                                                \
		else if (down == 0)                                                     \
			tpd_down_status = 0;                                                \
		}                                                                           \
		else {                                                                      \
			if (tpd_em_log_to_fs) {                                                 \
				tpd_em_log_release();                                               \
			}                                                                       \
		}                                                                           \
	} while (0)

#ifdef TPD_DEBUG_TRACK
extern void *dal_fb_addr;
extern int tpd_debug_track;
void tpd_up_debug_track(int x, int y);
void tpd_down_debug_track(int x, int y);
#define TPD_UP_DEBUG_TRACK(x, y) do { if (tpd_debug_track) tpd_up_debug_track(x, y); } while (0)
#define TPD_DOWN_DEBUG_TRACK(x, y) do { if (tpd_debug_track) tpd_down_debug_track(x, y); } while (0)

#endif				/* TPD_DEBUG_TRACK */
#endif				/* TPD_DEBUG_CODE */

/* Macros that will be embedded in code */

#ifndef TPD_DEBUG_CHECK_NO_RESPONSE
#define TPD_DEBUG_CHECK_NO_RESPONSE
#endif

#ifndef TPD_DEBUG_SET_TIME
#define TPD_DEBUG_SET_TIME
#endif

#ifndef TPD_DEBUG_PRINT_UP
#define TPD_DEBUG_PRINT_UP
#endif

#ifndef TPD_DEBUG_PRINT_DOWN
#define TPD_DEBUG_PRINT_DOWN
#endif

#ifndef TPD_UP_DEBUG_TRACK
#define TPD_UP_DEBUG_TRACK(x, y)
#endif

#ifndef TPD_DOWN_DEBUG_TRACK
#define TPD_DOWN_DEBUG_TRACK(x, y)
#endif


#endif				/* TPD_DEBUG_H */
