#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <asm/system.h>
#include <mach/mt_reg_base.h>
#include <mach/mt_irq.h>
#include <linux/debugfs.h>
#include <linux/timer.h>
#include "seninf_reg.h"

#define SENINF_REG_RANG	   (0x1000)	/* same as seninf_reg.h but page-aligned */


struct seninf_event_counter {
	uint64_t frame_cntr;
	uint64_t pkt_frame_cntr;
	uint64_t crc_err;
	uint64_t ecc_single;
	uint64_t ecc_double;
};

#define QUEUE_SIZE 128

struct seninf_intr_mgr {
	seninf_reg_t *registers;
	atomic_t intr_cnt;
	atomic_t missed_intr_cnt;
	seninf_reg_t queue[QUEUE_SIZE];
	atomic_t active[QUEUE_SIZE];
	struct mutex lock;
	spinlock_t slock;
	int write_index;
	int read_index;
	struct work_struct async_work;
	struct seninf_event_counter seninf1_event_cntr;
	struct seninf_event_counter seninf2_event_cntr;
};

struct timer_list seninf_int_enable_timer;
struct seninf_intr_mgr g_sim;


static int frame_cntr1_debugfs_u64_get(void *data, u64 *val)
{
	if (g_sim.registers->SENINF1_CSI2_CTRL.Bits.CSI2_EN)
		*val = g_sim.registers->SENINF1_CSI2_SHORT_INFO.Bits.CSI2_FRAME_NO;
	else
		*val = g_sim.registers->SENINF1_NCSI2_FRAME_LINE_NUM.Bits.FRAME_NUM;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fc1_fops_u64_ro, frame_cntr1_debugfs_u64_get, NULL, "%llu\n");

struct dentry *frame_cntr1_debugfs_create_u64(const char *name, umode_t mode, struct dentry *parent, u64 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fc1_fops_u64_ro);
}

static int frame_cntr2_debugfs_u64_get(void *data, u64 *val)
{
	if (g_sim.registers->SENINF2_CSI2_CTRL.Bits.CSI2_EN)
		*val = g_sim.registers->SENINF2_CSI2_SHORT_INFO.Bits.CSI2_FRAME_NO;
	else
		*val = g_sim.registers->SENINF2_NCSI2_FRAME_LINE_NUM.Bits.FRAME_NUM;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fc2_fops_u64_ro, frame_cntr2_debugfs_u64_get, NULL, "%llu\n");

struct dentry *frame_cntr2_debugfs_create_u64(const char *name, umode_t mode, struct dentry *parent, u64 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fc2_fops_u64_ro);
}

void seninf_add_debugfs_entry(struct seninf_intr_mgr *sim)
{
	struct dentry *seninf;
	struct dentry *seninf1, *seninf2;
	printk("Add ispif debugfs hierarchy \n");

	seninf = debugfs_create_dir("seninf_events", NULL);
	if (seninf) {
		seninf1 = debugfs_create_dir("seninf1", seninf);
		if (seninf1) {
			memset(&sim->seninf1_event_cntr, 0,
				sizeof(struct seninf_event_counter));	/* zero the counters */
			debugfs_create_u64("crc_err", 0444, seninf1,
					   &sim->seninf1_event_cntr.crc_err);
			debugfs_create_u64("ecc_single", 0444, seninf1,
					   &sim->seninf1_event_cntr.ecc_single);
			debugfs_create_u64("ecc_double", 0444, seninf1,
					   &sim->seninf1_event_cntr.ecc_double);
			debugfs_create_u64("frame_cntr", 0444, seninf1,
					   &sim->seninf1_event_cntr.frame_cntr);
			frame_cntr1_debugfs_create_u64("pkt_frame_cntr", 0444, seninf1,
					   &sim->seninf1_event_cntr.pkt_frame_cntr);
		}
		seninf2 = debugfs_create_dir("seninf2", seninf);
		if (seninf2) {
			memset(&sim->seninf2_event_cntr, 0,
					sizeof(struct seninf_event_counter));	/* zero the counters */
			debugfs_create_u64("crc_err", 0444, seninf2,
					   &sim->seninf2_event_cntr.crc_err);
			debugfs_create_u64("ecc_single", 0444, seninf2,
					   &sim->seninf2_event_cntr.ecc_single);
			debugfs_create_u64("ecc_double", 0444, seninf2,
					   &sim->seninf2_event_cntr.ecc_double);
			debugfs_create_u64("frame_cntr", 0444, seninf2,
					   &sim->seninf2_event_cntr.frame_cntr);
			frame_cntr2_debugfs_create_u64("pkt_frame_cntr", 0444, seninf2,
					   &sim->seninf2_event_cntr.pkt_frame_cntr);
		}
	}
}

void seninf_update_debugfs_counters(struct seninf_intr_mgr *sim,
				    seninf_reg_t *reg)
{

	spin_lock(&sim->slock);

/* NCSI2 */
	if (reg->SENINF1_NCSI2_INT_STATUS.Raw) {
		if (reg->SENINF1_NCSI2_INT_STATUS.Bits.FS)
			sim->seninf1_event_cntr.frame_cntr++;
		if (reg->SENINF1_NCSI2_INT_STATUS.Bits.ERR_ECC_CORRECTED)
			sim->seninf1_event_cntr.ecc_single++;
		if (reg->SENINF1_NCSI2_INT_STATUS.Bits.ERR_ECC_DOUBLE)
			sim->seninf1_event_cntr.ecc_double++;
		if (reg->SENINF1_NCSI2_INT_STATUS.Bits.ERR_CRC)
			sim->seninf1_event_cntr.crc_err++;
	}
/* CSI2 */
	if (reg->SENINF1_CSI2_INTSTA.Raw) {
		/* if (reg->SENINF1_CSI2_INTSTA.Bits.CSI2OUT_VSYNC)
			sim->seninf1_event_cntr.frame_cntr++;*/
		if (reg->SENINF1_CSI2_INTSTA.Bits.ECC_CORRECT_IRQ)
			sim->seninf1_event_cntr.ecc_single++;
		if (reg->SENINF1_CSI2_INTSTA.Bits.ECC_ERR_IRQ)
			sim->seninf1_event_cntr.ecc_double++;
		if (reg->SENINF1_CSI2_INTSTA.Bits.CRC_ERR_IRQ)
			sim->seninf1_event_cntr.crc_err++;
	}

/* NCSI2 */
	if (reg->SENINF2_NCSI2_INT_STATUS.Raw) {
		if (reg->SENINF2_NCSI2_INT_STATUS.Bits.FS)
			sim->seninf2_event_cntr.frame_cntr++;
		if (reg->SENINF2_NCSI2_INT_STATUS.Bits.ERR_ECC_CORRECTED)
			sim->seninf2_event_cntr.ecc_single++;
		if (reg->SENINF2_NCSI2_INT_STATUS.Bits.ERR_ECC_DOUBLE)
			sim->seninf2_event_cntr.ecc_double++;
		if (reg->SENINF2_NCSI2_INT_STATUS.Bits.ERR_CRC)
			sim->seninf2_event_cntr.crc_err++;

	}
/* CSI2 */
	if (reg->SENINF2_CSI2_INTSTA.Raw) {
		/* if (reg->SENINF2_CSI2_INTSTA.Bits.CSI2OUT_VSYNC)
			sim->seninf2_event_cntr.frame_cntr++;*/
		if (reg->SENINF2_CSI2_INTSTA.Bits.ECC_CORRECT_IRQ)
			sim->seninf2_event_cntr.ecc_single++;
		if (reg->SENINF2_CSI2_INTSTA.Bits.ECC_ERR_IRQ)
			sim->seninf2_event_cntr.ecc_double++;
		if (reg->SENINF2_CSI2_INTSTA.Bits.CRC_ERR_IRQ)
			sim->seninf2_event_cntr.crc_err++;

	}

/*
	sim->seninf1_event_cntr.frame_cntr = sim->registers->SENINF1_CSI2_SHORT_INFO.Bits.CSI2_FRAME_NO;
	sim->seninf2_event_cntr.frame_cntr = sim->registers->SENINF2_CSI2_SHORT_INFO.Bits.CSI2_FRAME_NO;
*/
	spin_unlock(&sim->slock);
}

void seninf_print(seninf_reg_t *reg, u64 frame_num1, u64 frame_num2)
{

	static u64 fn1, fn2;

	/* RFC MIPI Errors  NCSI2*/
	if (reg->SENINF1_NCSI2_INT_STATUS.Bits.ERR_CRC && (fn1 != frame_num1))
		pr_err
		    ("%s: SENINF1_NCSI2_INT_STATUS.Bits.ERR_CRC @ frame[%llu]\n",
		     __func__, frame_num1);
	else if (reg->SENINF1_NCSI2_INT_STATUS.Bits.ERR_ECC_DOUBLE && (fn1 != frame_num1))
		pr_err
		    ("%s: SENINF1_NCSI2_INT_STATUS.Bits.ERR_ECC_DOUBLE @ frame[%llu]\n",
		     __func__, frame_num1);
	else if (reg->SENINF1_NCSI2_INT_STATUS.Bits.ERR_ECC_CORRECTED && (fn1 != frame_num1))
		pr_err
		    ("%s: SENINF1_NCSI2_INT_STATUS.Bits.ERR_ECC_CORRECTED @ frame[%llu]\n",
		     __func__, frame_num1);
	else if (reg->SENINF1_NCSI2_INT_STATUS.Bits.FS)
		pr_debug("%s: SENINF1_NCSI2_INT_STATUS.Bits.FS @ frame[%llu]\n",
			 __func__, frame_num1);
	/* FFC MIPI Errors NCSI2*/
	else if (reg->SENINF2_NCSI2_INT_STATUS.Bits.ERR_CRC && (fn2 != frame_num2))
		pr_err
		    ("%s: SENINF2_NCSI2_INT_STATUS.Bits.ERR_CRC @ frame[%llu]\n",
		     __func__, frame_num2);
	else if (reg->SENINF2_NCSI2_INT_STATUS.Bits.ERR_ECC_DOUBLE && (fn2 != frame_num2))
		pr_err
		    ("%s: SENINF2_NCSI2_INT_STATUS.Bits.ERR_ECC_DOUBLE @ frame[%llu]\n",
		     __func__, frame_num2);
	else if (reg->SENINF2_NCSI2_INT_STATUS.Bits.ERR_ECC_CORRECTED && (fn2 != frame_num2))
		pr_err
		    ("%s: SENINF2_NCSI2_INT_STATUS.Bits.ERR_ECC_CORRECTED @ frame_num[%llu]\n",
		     __func__, frame_num2);
	else if (reg->SENINF2_NCSI2_INT_STATUS.Bits.FS)
		pr_debug("%s: SENINF2_NCSI2_INT_STATUS.Bits.FS @ frame[%llu]\n",
			 __func__, frame_num2);

	/* RFC MIPI Errors CSI2*/
	if (reg->SENINF1_CSI2_INTSTA.Bits.CRC_ERR_IRQ && (fn1 != frame_num1))
		pr_err
		    ("%s: SENINF1_CSI2_INTSTA.Bits.CRC_ERR_IRQ @ frame[%llu]\n",
		     __func__, frame_num1);
	else if (reg->SENINF1_CSI2_INTSTA.Bits.ECC_ERR_IRQ && (fn1 != frame_num1))
		pr_err
		    ("%s: SENINF1_CSI2_INTSTA.Bits.ECC_ERR_IRQ @ frame[%llu]\n",
		     __func__, frame_num1);
	else if (reg->SENINF1_CSI2_INTSTA.Bits.ECC_CORRECT_IRQ && (fn1 != frame_num1))
		pr_err
		    ("%s: SENINF1_CSI2_INTSTA.Bits.ECC_CORRECT_IRQ @ frame[%llu]\n",
		     __func__, frame_num1);

	/* FFC MIPI Errors CSI2*/
	if (reg->SENINF2_CSI2_INTSTA.Bits.CRC_ERR_IRQ && (fn2 != frame_num2))
		pr_err
		    ("%s: SENINF2_CSI2_INTSTA.Bits.CRC_ERR_IRQ @ frame[%llu]\n",
		     __func__, frame_num2);
	else if (reg->SENINF2_CSI2_INTSTA.Bits.ECC_ERR_IRQ && (fn2 != frame_num2))
		pr_err
		    ("%s: SENINF2_CSI2_INTSTA.Bits.ECC_ERR_IRQ @ frame[%llu]\n",
		     __func__, frame_num2);
	else if (reg->SENINF2_CSI2_INTSTA.Bits.ECC_CORRECT_IRQ && (fn2 != frame_num2))
		pr_err
		    ("%s: SENINF2_CSI2_INTSTA.Bits.ECC_CORRECT_IRQ @ frame[%llu]\n",
		     __func__, frame_num2);
	else if (reg->SENINF2_CSI2_INTSTA.Bits.CSI2OUT_VSYNC)
		pr_err
		    ("%s: SENINF2_CSI2_INTSTA.Bits.CSI2OUT_VSYNC @ frame[%llu]\n",
		     __func__, frame_num2);

	/* SENINF Interrupts */

	if (reg->SENINF1_INTSTA.Raw)
		pr_err("%s: SENINF2_INTSTA.Raw[%x]\n", __func__, reg->SENINF1_INTSTA.Raw);
	if (reg->SENINF2_INTSTA.Raw)
		pr_err("%s: SENINF2_INTSTA.Raw[%x]\n", __func__, reg->SENINF2_INTSTA.Raw);

	fn1 = frame_num1;
	fn2 = frame_num2;

}

void seninf_do_work(struct work_struct *data)
{
	struct seninf_intr_mgr *sim =
	    container_of(data, struct seninf_intr_mgr, async_work);
	int missed_intr_count = atomic_read(&sim->missed_intr_cnt);
	int intr_count = atomic_read(&sim->intr_cnt);
	seninf_reg_t *reg;
	int local_cntr = 0;

	if (intr_count == 0)
		return;

	mutex_lock(&sim->lock);	/* mutex lock is for workqueue concurency on different CPU's */
	if (missed_intr_count) {
		pr_debug("%s: queue_full missed %d interrupts's\n", __func__,
		       missed_intr_count);
		atomic_set(&sim->missed_intr_cnt, 0);
	}

	while (intr_count > 0 && local_cntr < QUEUE_SIZE) {
		pr_debug("%s reading element[%d] intr_cnt[%d]\n", __func__,
			 sim->read_index, intr_count);
		if (atomic_read(&sim->active[sim->read_index])) {
			reg = &sim->queue[sim->read_index];
			seninf_print(reg, sim->seninf1_event_cntr.pkt_frame_cntr,
				     sim->seninf2_event_cntr.pkt_frame_cntr);
			atomic_dec(&sim->intr_cnt);
		}
		/* increment read index */
		sim->read_index = (sim->read_index + 1) & QUEUE_SIZE;
		local_cntr++;	/* each time workqueue runs through QUEUE_SIZE elements and exits, this os for safety to make the workqueue run only QUEUE_SIZE number of loops */
		intr_count = atomic_read(&sim->intr_cnt);
	}
	mutex_unlock(&sim->lock);
}

void seninf_intr_mgr_init(struct seninf_intr_mgr *sim)
{
	int i;
	spin_lock_init(&sim->slock);
	mutex_init(&sim->lock);
	sim->write_index = 0;
	sim->read_index = 0;
	atomic_set(&sim->intr_cnt, 0);
	atomic_set(&sim->missed_intr_cnt, 0);

	for (i = 0; i < QUEUE_SIZE; i++)
		atomic_set(&sim->active[i], 0);

	INIT_WORK(&sim->async_work, seninf_do_work);
}

void seninf_intr_queue_dispatch(struct seninf_intr_mgr *sim)
{

	/* In case there is a storm of interrupts, we back off by disabling the
	 * IRQ for sometime and using a timer re-enable it back again */
	if (atomic_read(&sim->intr_cnt) > 32) {
		disable_irq_nosync(MT_SENINF_IRQ_ID);
		seninf_int_enable_timer.expires = jiffies + msecs_to_jiffies(300);
		add_timer(&seninf_int_enable_timer);
		pr_err("%s: irq  disable\n", __func__);
		return;
	}

	/* update intr and missed isr count */
	if (atomic_read(&sim->intr_cnt) > QUEUE_SIZE) {
		atomic_inc(&sim->missed_intr_cnt);
		pr_debug
		    ("%s: missed_intr[%d] SENINF1_NCSI2_INT_STATUS[%x] SENINF2_NCSI2_INT_STATUS[%x]\n",
		     __func__, atomic_read(&sim->missed_intr_cnt),
		     sim->registers->SENINF1_NCSI2_INT_STATUS.Raw,
		     sim->registers->SENINF1_NCSI2_INT_STATUS.Raw);
		return;
	} else {
		atomic_inc(&sim->intr_cnt);
	}

	/* make current index inactive */
	atomic_set(&sim->active[sim->write_index], 0);
	/* NCSI2 */
	sim->queue[sim->write_index].SENINF2_NCSI2_INT_STATUS.Raw =
	    sim->registers->SENINF2_NCSI2_INT_STATUS.Raw;
	sim->queue[sim->write_index].SENINF1_NCSI2_INT_STATUS.Raw =
	    sim->registers->SENINF1_NCSI2_INT_STATUS.Raw;
	/* CSI2 */
	sim->queue[sim->write_index].SENINF1_CSI2_INTSTA.Raw =
	    sim->registers->SENINF1_CSI2_INTSTA.Raw;
	sim->queue[sim->write_index].SENINF2_CSI2_INTSTA.Raw =
	    sim->registers->SENINF2_CSI2_INTSTA.Raw;
	seninf_update_debugfs_counters(sim, &sim->queue[sim->write_index]);
	/* make current index active */
	atomic_set(&sim->active[sim->write_index], 1);

	/* increment write index */
	sim->write_index = (sim->write_index + 1) & QUEUE_SIZE;	/* power of 2 size & gives same effect as % but faster */

	queue_work(system_wq, &sim->async_work);
}

irqreturn_t seninf_irq(int irq, void *data)
{

	struct seninf_intr_mgr *sim = (struct seninf_intr_mgr *)data;
	volatile seninf_reg_t *reg = sim->registers;
	reg->SENINF1_NCSI2_INT_EN.Raw = 0x38 | (1 << 12);
	reg->SENINF2_NCSI2_INT_EN.Raw = 0x38 | (1 << 12);
	reg->SENINF1_CSI2_INTEN.Bits.CRC_ERR_IRQ_EN = 1;
	reg->SENINF1_CSI2_INTEN.Bits.ECC_ERR_IRQ_EN = 1;
	reg->SENINF1_CSI2_INTEN.Bits.ECC_CORRECT_IRQ_EN = 1;
	reg->SENINF1_CSI2_VS.Bits.CSI2_VS_CTRL = 0x1;
	reg->SENINF1_CSI2_INTSTA.Raw = reg->SENINF1_CSI2_INTSTA.Raw & ~0x10; /* read clear */
	reg->SENINF2_CSI2_INTEN.Bits.CRC_ERR_IRQ_EN = 1;
	reg->SENINF2_CSI2_INTEN.Bits.ECC_ERR_IRQ_EN = 1;
	reg->SENINF2_CSI2_INTEN.Bits.ECC_CORRECT_IRQ_EN = 1;
	reg->SENINF2_CSI2_VS.Bits.CSI2_VS_CTRL = 0x1;
	reg->SENINF2_CSI2_INTSTA.Raw = reg->SENINF2_CSI2_INTSTA.Raw & ~0x10; /* read clear */
	reg->SENINF1_INTEN.Raw = 0;
	reg->SENINF2_INTEN.Raw = 0;

	seninf_intr_queue_dispatch(sim);

	return IRQ_HANDLED;
}

void mt_irq_set_sens(unsigned int irq, unsigned int sens);
void mt_irq_set_polarity(unsigned int irq, unsigned int polarity);

void seninf_irq_enable(unsigned long arg)
{
	pr_err("%s irq enable\n", __func__);
	enable_irq(MT_SENINF_IRQ_ID);
}


static int __init seninf_init(void)
{
	int ret = 0;
	pr_err("%s\n", __func__);
	/* IO remap the SENIF memory mapped registers */
	g_sim.registers =
	    (seninf_reg_t *) ioremap(SENINF_BASE_HW, SENINF_REG_RANG);

	seninf_intr_mgr_init(&g_sim);
	seninf_add_debugfs_entry(&g_sim);

	mt_irq_set_sens(MT_SENINF_IRQ_ID, MT_LEVEL_SENSITIVE);
	mt_irq_set_polarity(MT_SENINF_IRQ_ID, MT_POLARITY_LOW);

	/* timer to enable IRQ */
	init_timer(&seninf_int_enable_timer);
	seninf_int_enable_timer.function = seninf_irq_enable;
	seninf_int_enable_timer.data = 0;

	/* register IRQ */
	ret =
	    request_irq(MT_SENINF_IRQ_ID, (irq_handler_t) seninf_irq,
			IRQF_TRIGGER_LOW, "seninf", &g_sim);
	if (ret) {
		pr_err("%s: failed to request IRQ MT_SENINF_IRQ_ID ret[%d]\n",
		       __func__, ret);
	}

	return ret;
}

static void __exit seninf_exit(void)
{
	pr_err("%s\n", __func__);
	/* IO unmap the SENIF memory mapped registers */
	iounmap((void __iomem *)g_sim.registers);

	/* un-register IRQ */
	free_irq(MT_SENINF_IRQ_ID, NULL);

}

module_init(seninf_init);
module_exit(seninf_exit);

MODULE_DESCRIPTION("Sensor interface interrupt status monitor driver");
MODULE_AUTHOR
    ("Karthik Poduval<kpoduval@lab126.com or karthik.poduval@gmail.com>");
MODULE_LICENSE("GPL");
