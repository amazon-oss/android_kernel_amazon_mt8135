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
#include <mach/system.h>

#include "mach/mt_typedefs.h"
#include "mach/mt_thermal.h"
#include "mach/mt_cpufreq.h"
#include "mach/mt_gpufreq.h"

#include <linux/thermal_framework.h>
#include <linux/platform_data/mtk_thermal.h>

#define MTKTSCPU_TEMP_CRIT 120000

#define THERMAL_NAME "mtk-thermal"
#define TC2_THERMAL_NAME "mtk-thermal2"
#define y_curr_repeat_times 1

#define CONFIG_SUPPORT_MET_MTKTSCPU 1

#if CONFIG_SUPPORT_MET_MTKTSCPU
#include <linux/export.h>
#include <linux/met_drv.h>
#endif

static int mtktscpu_debug_log;

#define mtktscpu_dprintk(fmt, args...)   \
do {                                    \
	if (mtktscpu_debug_log) {                \
		xlog_printk(ANDROID_LOG_INFO, "Power/CPU_Thermal", fmt, ##args); \
	}                                   \
} while (0)

static kal_int32 temperature_to_raw_roomt(kal_uint32 ret);

int last_abb_t = 0;
static kal_int32 g_adc_ge;
static kal_int32 g_adc_oe;
static kal_int32 g_o_vtsmcu1;
static kal_int32 g_o_vtsmcu2;
static kal_int32 g_o_vtsmcu3;
static kal_int32 g_o_vtsabb;
static kal_int32 g_o_vtsmcu4;
static kal_int32 g_degc_cali;
static kal_int32 g_adc_cali_en;
static kal_int32 g_o_slope;
static kal_int32 g_o_slope_sign;
static kal_int32 g_id;

static kal_int32 g_ge;
static kal_int32 g_oe;
static kal_int32 g_gain;

static kal_int32 g_x_roomt[4] = { 0, 0, 0, 0 };

#if CONFIG_SUPPORT_MET_MTKTSCPU /* MET */

static char header[] =
    "met-info [000] 0.0: ms_ud_sys_header: CPU_Temp,"
    "TS1_temp,TS2_temp,TS3_temp,TS4_temp,d,d,d,d\n"
    "met-info [000] 0.0: ms_ud_sys_header: P_adaptive," "CPU_power,GPU_power,d,d\n";

static const char help[] = "  --mtktscpu                              monitor mtktscpu\n";

static int sample_print_help(char *buf, int len)
{
	return snprintf(buf, PAGE_SIZE, help);
}

static int sample_print_header(char *buf, int len)
{
	return snprintf(buf, PAGE_SIZE, header);
}

unsigned int met_mtktscpu_dbg = 0;

static void sample_start(void)
{
	met_mtktscpu_dbg = 1;
	return;
}

static void sample_stop(void)
{
	met_mtktscpu_dbg = 0;
	return;
}

struct metdevice met_mtktscpu = {
	.name = "mtktscpu",
	.owner = THIS_MODULE,
	.type = MET_TYPE_BUS,
	.start = sample_start,
	.stop = sample_stop,
	.print_help = sample_print_help,
	.print_header = sample_print_header,
};
EXPORT_SYMBOL(met_mtktscpu);
#endif /* MET */

static DEFINE_MUTEX(therm_lock);

struct mtktscpu_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct mtk_thermal_platform_data *pdata;
	unsigned long curr_temp;
	unsigned long prev_temp;
};

void get_thermal_ca7_slope_intercept(struct TS_PTPOD *ts_info)
{
	unsigned int temp0, temp1, temp2;
	struct TS_PTPOD ts_ptpod;

	temp0 = (10000 * 100000 / g_gain) * 15 / 18;
	if (g_o_slope_sign == 0)
		temp1 = temp0 / (165 + g_o_slope);
	else
		temp1 = temp0 / (165 - g_o_slope);
	ts_ptpod.ts_MTS = temp1;

	temp0 = (g_degc_cali * 10 / 2);
	temp1 = ((10000 * 100000 / 4096 / g_gain) * g_oe + g_x_roomt[1] * 10) * 15 / 18;
	if (g_o_slope_sign == 0)
		temp2 = temp1 * 10 / (165 + g_o_slope);
	else
		temp2 = temp1 * 10 / (165 - g_o_slope);

	ts_ptpod.ts_BTS = (temp0 + temp2 - 250) * 4 / 10;

	ts_info->ts_MTS = ts_ptpod.ts_MTS;
	ts_info->ts_BTS = ts_ptpod.ts_BTS;
	mtktscpu_dprintk("ts_MTS=%d, ts_BTS=%d\n", ts_ptpod.ts_MTS, ts_ptpod.ts_BTS);

	return;
}
EXPORT_SYMBOL(get_thermal_ca7_slope_intercept);

void get_thermal_ca15_slope_intercept(struct TS_PTPOD *ts_info)
{
	unsigned int temp0, temp1, temp2;
	struct TS_PTPOD ts_ptpod;

	temp0 = (10000 * 100000 / g_gain) * 15 / 18;
	if (g_o_slope_sign == 0)
		temp1 = temp0 / (165 + g_o_slope);
	else
		temp1 = temp0 / (165 - g_o_slope);

	ts_ptpod.ts_MTS = temp1;

	temp0 = (g_degc_cali * 10 / 2);
	temp1 = ((10000 * 100000 / 4096 / g_gain) * g_oe + g_x_roomt[3] * 10) * 15 / 18;
	if (g_o_slope_sign == 0)
		temp2 = temp1 * 10 / (165 + g_o_slope);
	else
		temp2 = temp1 * 10 / (165 - g_o_slope);

	ts_ptpod.ts_BTS = (temp0 + temp2 - 250) * 4 / 10;

	ts_info->ts_MTS = ts_ptpod.ts_MTS;
	ts_info->ts_BTS = ts_ptpod.ts_BTS;
	mtktscpu_dprintk("ts_MTS=%d, ts_BTS=%d\n", ts_ptpod.ts_MTS, ts_ptpod.ts_BTS);

	return;
}
EXPORT_SYMBOL(get_thermal_ca15_slope_intercept);

static irqreturn_t thermal_interrupt_handler(int irq, void *dev_id)
{
	kal_uint32 ret = 0;
	ret = DRV_Reg32(TEMPMONINTSTS);

	xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
		    "thermal_isr: [Interrupt trigger]: status = 0x%x\n", ret);
	if (ret & THERMAL_MON_CINTSTS0) {
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr: thermal sensor point 0 - cold interrupt trigger\n");
	}
	if (ret & THERMAL_MON_HINTSTS0) {
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr: thermal sensor point 0 - hot interrupt trigger\n");
	}

	if (ret & THERMAL_tri_SPM_State0)
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr: Thermal state0 to trigger SPM state0\n");
	if (ret & THERMAL_tri_SPM_State1)
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr: Thermal state1 to trigger SPM state1\n");
	if (ret & THERMAL_tri_SPM_State2)
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr: Thermal state2 to trigger SPM state2\n");

	return IRQ_HANDLED;
}

static irqreturn_t TC2_thermal_interrupt_handler(int irq, void *dev_id)
{
	kal_uint32 ret = 0;
	ret = DRV_Reg32(TC2_TEMPMONINTSTS);

	xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
		    "thermal_isr2: [Interrupt trigger]: status = 0x%x\n", ret);
	if (ret & THERMAL_MON_CINTSTS0) {
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr2: thermal sensor point 0 - cold interrupt trigger\n");
	}
	if (ret & THERMAL_MON_HINTSTS0) {
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr2: thermal sensor point 0 - hot interrupt trigger\n");
	}

	if (ret & THERMAL_tri_SPM_State0)
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr2: Thermal state0 to trigger SPM state0\n");
	if (ret & THERMAL_tri_SPM_State1)
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr2: Thermal state1 to trigger SPM state1\n");
	if (ret & THERMAL_tri_SPM_State2)
		xlog_printk(ANDROID_LOG_DEBUG, "[Power/CPU_Thermal]",
			    "thermal_isr2: Thermal state2 to trigger SPM state2\n");

	return IRQ_HANDLED;
}

static void thermal_reset_and_initial(void)
{
	UINT32 temp = 0;

	mtktscpu_dprintk("[Reset and init thermal controller]\n");

	temp = DRV_Reg32(PERI_GLOBALCON_RST0);
	temp |= 0x00010000;
	DRV_WriteReg32(PERI_GLOBALCON_RST0, temp);

	temp = DRV_Reg32(PERI_GLOBALCON_RST0);
	temp &= 0xFFFEFFFF;
	DRV_WriteReg32(PERI_GLOBALCON_RST0, temp);

	DRV_WriteReg32(TS_CON0_V, DRV_Reg32(TS_CON0_V) & 0xFFFFFF3F);   /* read tsmcu need */
	udelay(200);

	/* enable VBE_SEL */
	DRV_WriteReg32(TS_CON0_V, DRV_Reg32(TS_CON0_V) | 0x00000020);

	/* AuxADC Initialization */
	temp = DRV_Reg32(AUXADC_CON0_V);
	temp &= 0xFFFFF3FF;
	DRV_WriteReg32(AUXADC_CON0_V, temp);	/* disable auxadc channel 10,11 synchronous mode */

	DRV_WriteReg32(AUXADC_CON1_CLR_V, 0xC00);	/* disable auxadc channel 10,11 immediate mode */
	temp = DRV_Reg32(AUXADC_CON1_V);
	temp &= 0xFFFFF3FF;
	DRV_WriteReg32(AUXADC_CON1_V, temp);	/* disable auxadc channel 10, 11 immediate mode */


	DRV_WriteReg32(TEMPMONCTL1, 0x000003FF);	/* counting unit is 1024 / 66M = 15.5us */


	/* DRV_WriteReg32(TEMPMONCTL2, 0x02190219);      // sensing interval is 537 * 15.5us = 8.3235ms */
	DRV_WriteReg32(TEMPMONCTL2, 0x010C010C);	/* sensing interval is 268 * 15.5us = 4.154ms */

	/* DRV_WriteReg32(TEMPAHBPOLL, 0x00086470);      //total update time = 8.3235ms+ 8.3333ms = 16.6568ms */
	DRV_WriteReg32(TEMPAHBPOLL, 0x00043238);	/* total update time = 4.154ms+ 4.167ms = 8.321ms */
	DRV_WriteReg32(TEMPAHBTO, 0xFFFFFFFF);	/* exceed this polling time, IRQ would be inserted */

	DRV_WriteReg32(TEMPMONIDET0, 0x00000000);	/* times for interrupt occurrance */
	DRV_WriteReg32(TEMPMONIDET1, 0x00000000);	/* times for interrupt occurrance */
	DRV_WriteReg32(TEMPMONIDET2, 0x00000000);	/* times for interrupt occurrance */

	/* DRV_WriteReg32(TEMPMSRCTL0, 0x00000124);     // temperature sampling control, 10 sample */
	DRV_WriteReg32(TEMPMSRCTL0, 0x00000092);	/* temperature sampling control, 4 sample */

	/* DRV_WriteReg32(AUXADC_CON1_SET_V, 0x800);    // enable auxadc channel 11 immediate mode */

	DRV_WriteReg32(TEMPADCPNP0, 0x0);
	DRV_WriteReg32(TEMPADCPNP0, 0x1);
	DRV_WriteReg32(TEMPADCPNP0, 0x2);
	DRV_WriteReg32(TEMPPNPMUXADDR, TS_CON1_P);	/* AHB address for pnp sensor mux selection */

	DRV_WriteReg32(TEMPADCMUX, 0x800);
	DRV_WriteReg32(TEMPADCMUXADDR, AUXADC_CON1_SET_P);	/* AHB address for auxadc mux selection */

	DRV_WriteReg32(TEMPADCEN, 0x800);	/* AHB value for auxadc enable */
	DRV_WriteReg32(TEMPADCENADDR, AUXADC_CON1_CLR_P);

	DRV_WriteReg32(TEMPADCVALIDADDR, AUXADC_DAT11_P);	/* AHB address for auxadc valid bit */
	DRV_WriteReg32(TEMPADCVOLTADDR, AUXADC_DAT11_P);	/* AHB address for auxadc voltage output */
	DRV_WriteReg32(TEMPRDCTRL, 0x0);	/* read valid & voltage are at the same register */
	DRV_WriteReg32(TEMPADCVALIDMASK, 0x0000002C);	/* valid bit is (the 12th bit is valid bit and 1 is valid) */
	DRV_WriteReg32(TEMPADCVOLTAGESHIFT, 0x0);	/* do not need to shift */
	DRV_WriteReg32(TEMPADCWRITECTRL, 0x3);	/* enable auxadc mux & pnp mux write transaction */

	DRV_WriteReg32(TEMPMONCTL0, 0x00000007);	/* enable periodoc temperature sensing point 0,1,2 */

/* Thermal Controller 2 */
	DRV_WriteReg32(TC2_TEMPMONCTL1, 0x000003FF);	/* counting unit is 1024 / 66M = 15.5us */
	/* DRV_WriteReg32(TC2_TEMPMONCTL2, 0x02190219);            // sensing interval is 537 * 15.5us = 8.3235ms */
	/* total update time = 8.3235ms+ 8.3333ms = 16.6568ms */
	/* DRV_WriteReg32(TC2_TEMPAHBPOLL, 0x00086470);  */
	DRV_WriteReg32(TC2_TEMPMONCTL2, 0x010C010C);	/* sensing interval is 268 * 15.5us = 4.154ms */
	DRV_WriteReg32(TC2_TEMPAHBPOLL, 0x00043238);	/* total update time = 4.154ms+ 4.167ms = 8.321ms */
	DRV_WriteReg32(TC2_TEMPAHBTO, 0xFFFFFFFF);	/* exceed this polling time, IRQ would be inserted */
	DRV_WriteReg32(TC2_TEMPMONIDET0, 0x00000000);	/* times=1 for interrupt occurrance */
	DRV_WriteReg32(TC2_TEMPMONIDET1, 0x00000000);	/* times=1 for interrupt occurrance */
	DRV_WriteReg32(TC2_TEMPMONIDET2, 0x00000000);	/* times=1 for interrupt occurrance */
	/* DRV_WriteReg32(TC2_TEMPMSRCTL0, 0x00000124);            // (10 sampling) */
	DRV_WriteReg32(TC2_TEMPMSRCTL0, 0x00000092);	/* temperature measurement sampling control(4 sampling) */

	DRV_WriteReg32(TC2_TEMPADCPNP0, 0x3);	/* this value will be stored to TEMPPNPMUXADDR automatically by hw */
	DRV_WriteReg32(TC2_TEMPADCPNP1, 0x2);	/* this value will be stored to TEMPPNPMUXADDR automatically by hw */
	DRV_WriteReg32(TC2_TEMPADCPNP2, 0x1);	/* this value will be stored to TEMPPNPMUXADDR automatically by hw */
	DRV_WriteReg32(TC2_TEMPPNPMUXADDR, TS_CON2_P);	/* AHB address for pnp sensor mux selection */

	/* DRV_WriteReg32(AUXADC_CON1_SET, 0x400); // enable auxadc channel 10 immediate mode */

	DRV_WriteReg32(TC2_TEMPADCMUX, 0x400);	/* channel 10 */
	DRV_WriteReg32(TC2_TEMPADCMUXADDR, AUXADC_CON1_SET_P);	/* AHB address for auxadc mux selection */

	DRV_WriteReg32(TC2_TEMPADCEN, 0x400);	/* channel 10, AHB value for auxadc enable */
	DRV_WriteReg32(TC2_TEMPADCENADDR, AUXADC_CON1_CLR_P);	/* (channel 10 immediate mode selected) */

	DRV_WriteReg32(TC2_TEMPADCVALIDADDR, AUXADC_DAT10_P);	/* AHB address for auxadc valid bit */
	DRV_WriteReg32(TC2_TEMPADCVOLTADDR, AUXADC_DAT10_P);	/* AHB address for auxadc voltage output */
	DRV_WriteReg32(TC2_TEMPRDCTRL, 0x0);	/* read valid & voltage are at the same register */
	DRV_WriteReg32(TC2_TEMPADCVALIDMASK, 0x0000002C); /* the 12th bit is valid bit and 1 is valid */
	DRV_WriteReg32(TC2_TEMPADCVOLTAGESHIFT, 0x0);	/* do not need to shift */
	DRV_WriteReg32(TC2_TEMPADCWRITECTRL, 0x3);	/* enable auxadc mux & pnp write transaction */

	/* enable all interrupt, will enable in set_thermal_ctrl_trigger_SPM */
	/* DRV_WriteReg32(TC2_TEMPMONINT, 0x0007FFFF); */
	DRV_WriteReg32(TC2_TEMPMONCTL0, 0x00000007);	/* enable all sensing point (sensing point 0,1,2) */
}


static void set_thermal_ctrl_trigger_SPM(int temperature)
{
	int temp = 0;
	int raw_high, raw_middle, raw_low;

	mtktscpu_dprintk("[Set_thermal_ctrl_trigger_SPM]: temperature=%d\n", temperature);

	/* temperature to trigger SPM state2 */
	raw_high = temperature_to_raw_roomt(temperature);
	raw_middle = temperature_to_raw_roomt(40000);
	raw_low = temperature_to_raw_roomt(10000);

	temp = DRV_Reg32(TEMPMONINT);
	DRV_WriteReg32(TEMPMONINT, temp & 0x1FFFFFFF);	/* disable trigger SPM interrupt */

	DRV_WriteReg32(TEMPPROTCTL, 0x20000);
	DRV_WriteReg32(TEMPPROTTA, raw_low);
	DRV_WriteReg32(TEMPPROTTB, raw_middle);
	DRV_WriteReg32(TEMPPROTTC, raw_high);

	DRV_WriteReg32(TEMPMONINT, temp | 0x80000000);	/* only enable trigger SPM high temp interrupt */

/* Thermal Controller 2 */
	temp = DRV_Reg32(TC2_TEMPMONINT);
	DRV_WriteReg32(TC2_TEMPMONINT, temp & 0x1FFFFFFF);	/* disable trigger SPM interrupt */

	DRV_WriteReg32(TC2_TEMPPROTCTL, 0x20000);
	DRV_WriteReg32(TC2_TEMPPROTTA, raw_low);
	DRV_WriteReg32(TC2_TEMPPROTTB, raw_middle);
	DRV_WriteReg32(TC2_TEMPPROTTC, raw_high);

	DRV_WriteReg32(TC2_TEMPMONINT, temp | 0x80000000);	/* only enable trigger SPM high temp interrupt */
}

static void thermal_cal_prepare(void)
{
	kal_uint32 temp0, temp1, temp2 = 0;

	temp0 = DRV_Reg32(0xF0009100);
	temp1 = DRV_Reg32(0xF0009104);
	temp2 = DRV_Reg32(0xF0009108);
	pr_info
	    ("[Power/CPU_Thermal] [Thermal calibration] ");
	pr_info
	    ("Reg(0xF0009100)=0x%x, Reg(0xF0009104)=0x%x, Reg(0xF0009108)=0x%x\n",
	     temp0, temp1, temp2);

	g_adc_ge = (temp1 & 0xFFC00000) >> 22;
	g_adc_oe = (temp1 & 0x003FF000) >> 12;

	g_o_vtsmcu1 = (temp0 & 0x03FE0000) >> 17;
	g_o_vtsmcu2 = (temp0 & 0x0001FF00) >> 8;
	g_o_vtsabb = (temp1 & 0x000001FF);
	g_o_vtsmcu3 = (temp2 & 0x03FE0000) >> 17;
	g_o_vtsmcu4 = (temp2 & 0x0001FF00) >> 8;

	g_degc_cali = (temp0 & 0x0000007E) >> 1;
	g_adc_cali_en = (temp0 & 0x00000001);
	g_o_slope_sign = (temp0 & 0x00000080) >> 7;
	g_o_slope = (temp0 & 0xFC000000) >> 26;

	g_id = (temp1 & 0x00000200) >> 9;
	if (g_id == 0)
		g_o_slope = 0;

	if (g_adc_cali_en == 1) {
		/* thermal_enable = true; */
	} else {
		g_adc_ge = 512;
		g_adc_oe = 512;
		g_degc_cali = 40;
		g_o_slope = 0;
		g_o_slope_sign = 0;
		g_o_vtsmcu1 = 260;
		g_o_vtsmcu2 = 260;
		g_o_vtsmcu3 = 260;
		g_o_vtsmcu4 = 260;
	}
	pr_info
	    ("[Power/CPU_Thermal] [Thermal calibration] g_adc_ge = 0x%x, g_adc_oe = 0x%x, g_degc_cali = 0x%x, ",
	     g_adc_ge, g_adc_oe, g_degc_cali);
	pr_info
	    ("g_adc_cali_en = 0x%x, g_o_slope = 0x%x, g_o_slope_sign = 0x%x, g_id = 0x%x\n",
	     g_adc_cali_en, g_o_slope, g_o_slope_sign, g_id);
	pr_info
	    ("[Power/CPU_Thermal] [Thermal calibration] g_o_vtsmcu1 = 0x%x, g_o_vtsmcu2 = 0x%x, ",
	     g_o_vtsmcu1, g_o_vtsmcu2);
	pr_info
	    ("g_o_vtsmcu3 = 0x%x, g_o_vtsabb = 0x%x, g_o_vtsmcu4 = 0x%x\n",
	     g_o_vtsmcu3, g_o_vtsabb, g_o_vtsmcu4);
}

static void thermal_cal_prepare_2(kal_uint32 ret)
{
	kal_int32 format_1, format_2, format_3, format_4 = 0;

	g_ge = ((g_adc_ge - 512) * 10000) / 4096;	/* ge * 10000 */
	g_oe = (g_adc_oe - 512);
	g_gain = (10000 + g_ge);	/* gain * 10000 */

	format_1 = (g_o_vtsmcu1 + 3350 - g_oe);
	format_2 = (g_o_vtsmcu2 + 3350 - g_oe);
	format_3 = (g_o_vtsmcu3 + 3350 - g_oe);
	format_4 = (g_o_vtsmcu4 + 3350 - g_oe);

	g_x_roomt[0] = (((format_1 * 10000) / 4096) * 10000) / g_gain;	/* x_roomt * 10000 */
	g_x_roomt[1] = (((format_2 * 10000) / 4096) * 10000) / g_gain;	/* x_roomt * 10000 */
	g_x_roomt[2] = (((format_3 * 10000) / 4096) * 10000) / g_gain;	/* x_roomt * 10000 */
	g_x_roomt[3] = (((format_4 * 10000) / 4096) * 10000) / g_gain;	/* x_roomt * 10000 */

	pr_info
	    ("[Power/CPU_Thermal] [Thermal calibration] g_ge = %d, g_oe = %d, g_gain = %d, ",
	     g_ge, g_oe, g_gain);
	pr_info
	    ("g_x_roomt1 = %d, g_x_roomt2 = %d, g_x_roomt3 = %d, g_x_roomt4 = %d\n",
	     g_x_roomt[0], g_x_roomt[1], g_x_roomt[2], g_x_roomt[3]);
}

static void raw_to_temperature_roomt(UINT32 raw[], UINT32 temp[])
{
	int i = 0;
	int y_curr[4];
	int format_1 = 0;
	int format_2[4];
	int format_3[4];
	int format_4[4];

	for (i = 0; i < 4; i++) {
		y_curr[i] = raw[i];
		format_2[i] = 0;
		format_3[i] = 0;
		format_4[i] = 0;
	}

	mtktscpu_dprintk
	    ("[Power/CPU_Thermal]: g_ge = %d, g_oe = %d, g_gain = %d, ",
	     g_ge, g_oe, g_gain);
	mtktscpu_dprintk
	    ("g_x_roomt1 = %d, g_x_roomt2 = %d, g_x_roomt3 = %d, g_x_roomt4 = %d\n",
	     g_x_roomt[0], g_x_roomt[1], g_x_roomt[2], g_x_roomt[3]);

	format_1 = (g_degc_cali / 2);
	mtktscpu_dprintk("[Power/CPU_Thermal]: format_1=%d\n", format_1);

	for (i = 0; i < 4; i++) {
		format_2[i] = (y_curr[i] - g_oe);
		format_3[i] = (((((format_2[i]) * 10000) / 4096) * 10000) / g_gain)
			- g_x_roomt[i];	/* 10000 * format3 */
		format_3[i] = (format_3[i] * 15) / 18;
		if (g_o_slope_sign == 0)
			format_4[i] = ((format_3[i] * 100) / (165 + g_o_slope));	/* uint = 0.1 deg */
		else
			format_4[i] = ((format_3[i] * 100) / (165 - g_o_slope));	/* uint = 0.1 deg */

		format_4[i] = format_4[i] - (2 * format_4[i]);

		if (y_curr[i] == 0)
			temp[i] = 0;
		else
			temp[i] = (format_1 * 10) + format_4[i];	/* uint = 0.1 deg */

		mtktscpu_dprintk("[Power/CPU_Thermal]: format_2[%d]=%d, format_3[%d]=%d\n", i,
				 format_2[i], i, format_3[i]);
	}

	return;
}

static kal_int32 temperature_to_raw_roomt(kal_uint32 ret)
{
	/* Ycurr = [(Tcurr - DEGC_cali/2)*(-1)*(165+O_slope)*(18/15)*(1/100000)+X_roomtx]*Gain*4096 + OE */
	/* Tcurr unit is 1000 degC */

	kal_int32 t_curr = ret;
	kal_int32 format_1 = 0;
	kal_int32 format_2 = 0;
	kal_int32 format_3[4] = { 0 };
	kal_int32 format_4[4] = { 0 };
	kal_int32 i, index = 0, temp = 0;

	if (g_o_slope_sign == 0) {
		format_1 = t_curr - (g_degc_cali * 1000 / 2);
		format_2 = format_1 * (165 + g_o_slope) * 18 / 15;
		format_2 = format_2 - 2 * format_2;
		for (i = 0; i < 4; i++) {
			format_3[i] = format_2 / 1000 + g_x_roomt[i] * 10;
			format_4[i] = (format_3[i] * 4096 / 10000 * g_gain) / 100000 + g_oe;
			mtktscpu_dprintk
			    ("[Temperature_to_raw_roomt][roomt%d] format_1=%d, format_2=%d, format_3=%d, format_4=%d\n",
			     i, format_1, format_2, format_3[i], format_4[i]);
		}
	} else {
		format_1 = t_curr - (g_degc_cali * 1000 / 2);
		format_2 = format_1 * (165 - g_o_slope) * 18 / 15;
		format_2 = format_2 - 2 * format_2;
		for (i = 0; i < 4; i++) {
			format_3[i] = format_2 / 1000 + g_x_roomt[i] * 10;
			format_4[i] = (format_3[i] * 4096 / 10000 * g_gain) / 100000 + g_oe;
			mtktscpu_dprintk
			    ("[Temperature_to_raw_roomt][roomt%d] format_1=%d, format_2=%d, format_3=%d, format_4=%d\n",
			     i, format_1, format_2, format_3[i], format_4[i]);
		}
	}

	temp = 0;
	for (i = 0; i < 4; i++) {
		if (temp < format_4[i]) {
			temp = format_4[i];
			index = i;
		}
	}

	mtktscpu_dprintk("[Temperature_to_raw_roomt] temperature=%d, raw[%d]=%d", ret, index,
			 format_4[index]);
	return format_4[index];
}

static void thermal_calibration(void)
{
	if (g_adc_cali_en == 0)
		pr_info("Not Calibration\n");
	thermal_cal_prepare_2(0);
}

static DEFINE_MUTEX(TS_lock);
static int MA_counter = 0, MA_first_time, MA_len = 1;

static int CPU_Temp(void)
{
	int curr_raw[4], curr_temp[4];
	int con, index = 0, max_temp;

	curr_raw[0] = DRV_Reg32(TEMPMSR0);
	curr_raw[1] = DRV_Reg32(TEMPMSR1);
	curr_raw[2] = DRV_Reg32(TEMPMSR2);
	curr_raw[3] = DRV_Reg32(TC2_TEMPMSR0);
	curr_raw[0] = curr_raw[0] & 0x0fff;
	curr_raw[1] = curr_raw[1] & 0x0fff;
	curr_raw[2] = curr_raw[2] & 0x0fff;
	curr_raw[3] = curr_raw[3] & 0x0fff;
	raw_to_temperature_roomt(curr_raw, curr_temp);
	curr_temp[0] = curr_temp[0] * 100;
	curr_temp[1] = curr_temp[1] * 100;
	curr_temp[2] = curr_temp[2] * 100;
	curr_temp[3] = curr_temp[3] * 100;

	max_temp = 0;
	for (con = 0; con < 4; con++) {
		if (max_temp < curr_temp[con]) {
			max_temp = curr_temp[con];
			index = con;
		}
	}

#if CONFIG_SUPPORT_MET_MTKTSCPU
	if (met_mtktscpu_dbg) {
		trace_printk("%d,%d,%d,%d\n", curr_temp[0], curr_temp[1], curr_temp[2],
			     curr_temp[3]);
	}
#endif

	mtktscpu_dprintk
	    ("[CPU_Temp] temp[0]=%d, raw[0]=%d, temp[1]=%d, raw[1]=%d, temp[2]=%d, raw[2]=%d, temp[3]=%d, raw[3]=%d\n",
	     curr_temp[0], curr_raw[0], curr_temp[1], curr_raw[1], curr_temp[2], curr_raw[2],
	     curr_temp[3], curr_raw[3]);
	mtktscpu_dprintk("[CPU_Temp] max_temp=%d, index=%d\n", max_temp, index);

	return max_temp;
}

static int mtktscpu_get_TC_temp(void)
{
	int len = 0, i = 0;
	int t_ret1 = 0;
	static int abb[60] = { 0 };

	mutex_lock(&TS_lock);

	t_ret1 = CPU_Temp();
	abb[MA_counter] = t_ret1;
/* mtktscpu_dprintk("[mtktscpu_get_temp] temp=%d, raw=%d\n", t_ret1, curr_raw); */

	if (MA_counter == 0 && MA_first_time == 0 && MA_len != 1) {
		MA_counter++;

		t_ret1 = CPU_Temp();
		abb[MA_counter] = t_ret1;
/* mtktscpu_dprintk("[mtktscpu_get_temp] temp=%d, raw=%d\n", t_ret1, curr_raw); */
	}
	MA_counter++;
	if (MA_first_time == 0)
		len = MA_counter;
	else
		len = MA_len;

	t_ret1 = 0;
	for (i = 0; i < len; i++)
		t_ret1 += abb[i];

	last_abb_t = t_ret1 / len;

	mtktscpu_dprintk("[mtktscpu_get_hw_temp] MA_ABB=%d,\n", last_abb_t);
	mtktscpu_dprintk("[mtktscpu_get_hw_temp] MA_counter=%d, MA_first_time=%d, MA_len=%d\n",
			 MA_counter, MA_first_time, MA_len);

	if (MA_counter == MA_len) {
		MA_counter = 0;
		MA_first_time = 1;
	}

	mutex_unlock(&TS_lock);
	return last_abb_t;
}

static int mtktscpu_get_temp(struct thermal_zone_device *thermal, unsigned long *t)
{
	int curr_temp;
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;

	if (!tzone)
		return -EINVAL;

	curr_temp = mtktscpu_get_TC_temp();
	if ((curr_temp > 120000) | (curr_temp < -30000))
		pr_notice("[Power/CPU_Thermal] CPU T=%d\n", curr_temp);

	*t = (unsigned long)curr_temp;
	tzone->prev_temp = tzone->curr_temp;
	tzone->curr_temp = curr_temp;
	return 0;
}

static int mtktscpu_get_mode(struct thermal_zone_device *thermal, enum thermal_device_mode *mode)
{
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;

	*mode = pdata->mode;
	return 0;
}

static int mtktscpu_set_mode(struct thermal_zone_device *thermal, enum thermal_device_mode mode)
{
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;

	pdata->mode = mode;
	schedule_work(&tzone->therm_work);
	return 0;
}

static int mtktscpu_get_trip_type(struct thermal_zone_device *thermal,
				  int trip,
				  enum thermal_trip_type *type)
{
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;
	if (trip >= pdata->num_trips)
		return -EINVAL;

	*type = pdata->trips[trip].type;
	return 0;
}

static int mtktscpu_get_trip_temp(struct thermal_zone_device *thermal,
				  int trip,
				  unsigned long *t)
{
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;
	if (trip >= pdata->num_trips)
		return -EINVAL;

	*t = pdata->trips[trip].temp;
	return 0;
}

static int mtktscpu_set_trip_temp(struct thermal_zone_device *thermal,
				  int trip,
				  unsigned long t)
{
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;
	if (trip >= pdata->num_trips)
		return -EINVAL;

	pdata->trips[trip].temp = t;
	return 0;
}

static int mtktscpu_get_crit_temp(struct thermal_zone_device *thermal, unsigned long *t)
{
	int i;
	struct mtktscpu_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;

	for (i = 0; i < pdata->num_trips; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*t = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}

static int mtktscpu_thermal_notify(struct thermal_zone_device *thermal,
				int trip, enum thermal_trip_type type)
{
	return 0;
}

static struct thermal_zone_device_ops mtktscpu_dev_ops = {
	.get_temp = mtktscpu_get_temp,
	.get_mode = mtktscpu_get_mode,
	.set_mode = mtktscpu_set_mode,
	.get_trip_type = mtktscpu_get_trip_type,
	.get_trip_temp = mtktscpu_get_trip_temp,
	.set_trip_temp = mtktscpu_set_trip_temp,
	.get_crit_temp = mtktscpu_get_crit_temp,
	.notify = mtktscpu_thermal_notify,
};

static void mtktscpu_work(struct work_struct *work)
{
	struct mtktscpu_thermal_zone *tzone;
	struct mtk_thermal_platform_data *pdata;

	mutex_lock(&therm_lock);
	tzone = container_of(work, struct mtktscpu_thermal_zone, therm_work);
	if (!tzone)
		return;
	pdata = tzone->pdata;
	if (!pdata)
		return;
	if (pdata->mode == THERMAL_DEVICE_ENABLED)
		thermal_zone_device_update(tzone->tz);
	mutex_unlock(&therm_lock);
}

static int mtktscpu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct mtktscpu_thermal_zone *tzone;
	struct mtk_thermal_platform_data *pdata = dev_get_platdata(&pdev->dev);

	if (!pdata)
		return -EINVAL;

	thermal_cal_prepare();
	thermal_calibration();
	DRV_WriteReg32(TS_CON0_V, DRV_Reg32(TS_CON0_V) | 0x000000C0); /* turn off the sensor buffer to save power */
	thermal_reset_and_initial();

	tzone = devm_kzalloc(&pdev->dev, sizeof(*tzone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;

	memset(tzone, 0, sizeof(*tzone));
	tzone->pdata = pdata;
	tzone->tz = thermal_zone_device_register("mtktscpu",
						 pdata->num_trips,
						 0x3,
						 tzone,
						 &mtktscpu_dev_ops,
						 &pdata->tzp,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s Failed to register mtktscpu thermal zone device\n", __func__);
		return -EINVAL;
	}

	ret = request_irq(MT_PTP_THERM_IRQ_ID,
			  thermal_interrupt_handler,
			  IRQF_TRIGGER_LOW,
			  THERMAL_NAME,
			  NULL);
	if (ret)
		pr_err("%s IRQ register fail\n", __func__);

	ret = request_irq(MT_CA15_PTP_THERM_IRQ_ID,
			  TC2_thermal_interrupt_handler,
			  IRQF_TRIGGER_LOW,
			  TC2_THERMAL_NAME,
			  NULL);
	if (ret)
		pr_err("%s IRQ TC2 register fail\n", __func__);

	platform_set_drvdata(pdev, tzone);
	pdata->mode = THERMAL_DEVICE_ENABLED;
	INIT_WORK(&tzone->therm_work, mtktscpu_work);


	return 0;
}

static int mtktscpu_remove(struct platform_device *pdev)
{
	struct mtktscpu_thermal_zone *tzone = platform_get_drvdata(pdev);
	if (tzone) {
		cancel_work_sync(&tzone->therm_work);
		if (tzone->tz)
			thermal_zone_device_unregister(tzone->tz);
		kfree(tzone);
	}
	return 0;
}

static int mtktscpu_suspend(struct platform_device *dev, pm_message_t state)
{
	int temp = 0;
	int cnt = 0;

#if 1				/* CJ's suggestion, from Jerry.Wu */
	cnt = 0;
	while (cnt < 10) {	/* if bit7 and bit0=0 */
		temp = DRV_Reg32(TEMPMSRCTL1);
		mtktscpu_dprintk("TEMPMSRCTL1 = 0x%x\n", temp);
		/*
		   TEMPMSRCTL1[7]:Temperature measurement bus status[1]
		   TEMPMSRCTL1[0]:Temperature measurement bus status[0]

		   00: IDLE                           <=can pause    ,TEMPMSRCTL1[7][0]=0x00
		   01: Write transaction                                 <=can not pause,TEMPMSRCTL1[7][0]=0x01
		   10: Read transaction                          <=can not pause,TEMPMSRCTL1[7][0]=0x10
		   11: Waiting for read after Write   <=can pause    ,TEMPMSRCTL1[7][0]=0x11
		 */
		if (((temp & 0x81) == 0x00) || ((temp & 0x81) == 0x81)) {
			/*
			   Pause periodic temperature measurement for sensing point 0,sensing point 1,sensing point 2
			 */
			/* set bit1=bit2=bit3=1 to pause sensing point 0,1,2 */
			DRV_WriteReg32(TEMPMSRCTL1, (temp | 0x0E));

			break;
		}
		mtktscpu_dprintk("temp=0x%x, cnt=%d\n", temp, cnt);
		udelay(10);
		cnt++;
	}

	cnt = 0;
	while (cnt < 10) {	/* if bit7 and bit0=0 */
		temp = DRV_Reg32(TC2_TEMPMSRCTL1);
		mtktscpu_dprintk("TC2_TEMPMSRCTL1 = 0x%x\n", temp);
		/*
		   TEMPMSRCTL1[7]:Temperature measurement bus status[1]
		   TEMPMSRCTL1[0]:Temperature measurement bus status[0]

		   00: IDLE                           <=can pause    ,TEMPMSRCTL1[7][0]=0x00
		   01: Write transaction                                 <=can not pause,TEMPMSRCTL1[7][0]=0x01
		   10: Read transaction                          <=can not pause,TEMPMSRCTL1[7][0]=0x10
		   11: Waiting for read after Write   <=can pause    ,TEMPMSRCTL1[7][0]=0x11
		 */
		if (((temp & 0x81) == 0x00) || ((temp & 0x81) == 0x81)) {
			/*
			   Pause periodic temperature measurement for sensing point 0,sensing point 1,sensing point 2
			 */
			/* set bit1=bit2=bit3=1 to pause sensing point 0,1,2 */
			DRV_WriteReg32(TC2_TEMPMSRCTL1, (temp | 0x0E));

			break;
		}
		mtktscpu_dprintk("temp=0x%x, cnt=%d\n", temp, cnt);
		udelay(10);
		cnt++;
	}
#endif

	/* disable ALL periodoc temperature sensing point */
	DRV_WriteReg32(TEMPMONCTL0, 0x00000000);
	DRV_WriteReg32(TC2_TEMPMONCTL0, 0x00000000);

	DRV_WriteReg32(TS_CON0_V, DRV_Reg32(TS_CON0_V) | 0x000000C0);	/* turn off the sensor buffer to save power */

	return 0;
}

static int mtktscpu_resume(struct platform_device *dev)
{
	thermal_reset_and_initial();
	set_thermal_ctrl_trigger_SPM(MTKTSCPU_TEMP_CRIT);
	return 0;
}

static struct platform_driver mtk_thermal_driver = {
	.probe = mtktscpu_probe,
	.remove = mtktscpu_remove,
	.suspend = mtktscpu_suspend,
	.resume = mtktscpu_resume,
	.driver = {
		.name = THERMAL_NAME,
	},
};

static int __init mtktscpu_init(void)
{
	return platform_driver_register(&mtk_thermal_driver);
}

static void __exit mtktscpu_exit(void)
{
	platform_driver_unregister(&mtk_thermal_driver);
}

module_init(mtktscpu_init);
module_exit(mtktscpu_exit);
