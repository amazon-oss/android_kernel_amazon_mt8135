#include <linux/module.h>
#include <linux/slab.h>
#include <linux/aee.h>
#include <linux/xlog.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/mt_sched_mon.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/stacktrace.h>
#include <linux/mm.h>
#include <asm/stacktrace.h>
#include <asm/memory.h>
#include <asm/irq_regs.h>
#include <mach/fiq_smp_call.h>
#include <mach/irqs.h>
#include <mach/wd_api.h>
#include <mach/smp.h>
#include <mach/mtk_rtc.h>
#include "aee-common.h"

#ifdef CONFIG_MDUMP
#include <linux/mdump.h>
#endif

#define THREAD_INFO(sp) ((struct thread_info *) \
				((unsigned long)(sp) & ~(THREAD_SIZE - 1)))

#define WDT_PERCPU_LOG_SIZE	1024
#define WDT_LOG_DEFAULT_SIZE	4096
#define WDT_SAVE_STACK_SIZE		256
#define MAX_EXCEPTION_FRAME		16

/* NR_CPUS may not eaqual to real cpu numbers, alloc buffer at initialization */
static char *wdt_percpu_log_buf[NR_CPUS];
static int wdt_percpu_log_length[NR_CPUS];
static char wdt_log_buf[WDT_LOG_DEFAULT_SIZE];
static int wdt_percpu_preempt_cnt[NR_CPUS];
static unsigned long wdt_percpu_stackframe[NR_CPUS][MAX_EXCEPTION_FRAME];
static int wdt_log_length;
static atomic_t wdt_enter_fiq;

static struct {
	char bin_buf[WDT_SAVE_STACK_SIZE];
	int real_len;
	unsigned long top;
	unsigned long bottom;
} stacks_buffer_bin[NR_CPUS];

static struct {
	struct pt_regs regs;
	int real_len;
} regs_buffer_bin[NR_CPUS];


int in_fiq_handler(void)
{
	return atomic_read(&wdt_enter_fiq);
}

void aee_wdt_dump_info(void)
{
	char *printk_buf = wdt_log_buf;
	struct task_struct *task;
	int cpu, i;
	task = &init_task;

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_KE_WDT_INFO);
	if (wdt_log_length == 0) {
		pr_err("\n No log for WDT\n");
		mt_dump_sched_traces();
#ifdef CONFIG_SCHED_DEBUG
		sysrq_sched_debug_show_at_KE();
#endif
		return;
	}

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_KE_WDT_PERCPU);
	pr_err("==========================================");
	for (cpu = 0; cpu < num_possible_cpus(); cpu++) {
		if ((wdt_percpu_log_buf[cpu]) && (wdt_percpu_log_length[cpu])) {
			/* pr_err("=====> wdt_percpu_log_buf[%d], length=%d ", cpu, wdt_percpu_log_length[cpu]); */
			pr_err("%s", wdt_percpu_log_buf[cpu]);
			pr_err("Backtrace : ");
			for (i = 0; i < MAX_EXCEPTION_FRAME; i++) {
				if (wdt_percpu_stackframe[cpu][i] == 0)
					break;
				pr_info("%08lx, ", wdt_percpu_stackframe[cpu][i]);
			}
			pr_err("==========================================");
		}
	}
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_KE_WDT_LOG);
	/* printk temporary buffer printk_buf[1024]. To avoid char loss, add 4 bytes here */
	while (wdt_log_length > 0) {
		pr_err("%s", printk_buf);
		printk_buf += 1020;
		wdt_log_length -= 1020;
	}

#ifdef CONFIG_SCHED_DEBUG
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_KE_SCHED_DEBUG);
	sysrq_sched_debug_show_at_KE();
#endif

	for_each_process(task) {
		if (task->state == 0) {
			pr_err("PID: %d, name: %s\n", task->pid, task->comm);
			show_stack(task, NULL);
			pr_err("\n");
		}
	}

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_KE_WDT_DONE);
}

void aee_wdt_percpu_printf(int cpu, const char *fmt, ...)
{
	va_list args;

	if (wdt_percpu_log_buf[cpu] == NULL)
		return;

	va_start(args, fmt);
	wdt_percpu_log_length[cpu] +=
	    vsnprintf((wdt_percpu_log_buf[cpu] + wdt_percpu_log_length[cpu]),
		      (WDT_PERCPU_LOG_SIZE - wdt_percpu_log_length[cpu]), fmt, args);
	va_end(args);
}

void aee_wdt_printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	wdt_log_length += vsnprintf((wdt_log_buf + wdt_log_length),
				    (sizeof(wdt_log_buf) - wdt_log_length), fmt, args);
	va_end(args);
}


#if defined(CONFIG_FIQ_GLUE)
/* save registers in bin buffer, may comes from various cpu */
static void aee_dump_cpu_reg_bin(int cpu, void *regs_ptr)
{
	memcpy(&(regs_buffer_bin[cpu].regs), regs_ptr, sizeof(struct pt_regs));
	regs_buffer_bin[cpu].real_len = sizeof(struct pt_regs);

	aee_wdt_percpu_printf(cpu, "pc  : %08lx, lr : %08lx, cpsr : %08lx\n",
			      ((struct pt_regs *)regs_ptr)->ARM_pc,
			      ((struct pt_regs *)regs_ptr)->ARM_lr,
			      ((struct pt_regs *)regs_ptr)->ARM_cpsr);
	aee_wdt_percpu_printf(cpu, "sp  : %08lx, ip : %08lx, fp : %08lx\n",
			      ((struct pt_regs *)regs_ptr)->ARM_sp,
			      ((struct pt_regs *)regs_ptr)->ARM_ip,
			      ((struct pt_regs *)regs_ptr)->ARM_fp);
	aee_wdt_percpu_printf(cpu, "r10 : %08lx, r9 : %08lx, r8 : %08lx\n",
			      ((struct pt_regs *)regs_ptr)->ARM_r10,
			      ((struct pt_regs *)regs_ptr)->ARM_r9,
			      ((struct pt_regs *)regs_ptr)->ARM_r8);
	aee_wdt_percpu_printf(cpu, "r7  : %08lx, r6 : %08lx, r5 : %08lx\n",
			      ((struct pt_regs *)regs_ptr)->ARM_r7,
			      ((struct pt_regs *)regs_ptr)->ARM_r6,
			      ((struct pt_regs *)regs_ptr)->ARM_r5);
	aee_wdt_percpu_printf(cpu, "r4  : %08lx, r3 : %08lx, r2 : %08lx\n",
			      ((struct pt_regs *)regs_ptr)->ARM_r4,
			      ((struct pt_regs *)regs_ptr)->ARM_r3,
			      ((struct pt_regs *)regs_ptr)->ARM_r2);
	aee_wdt_percpu_printf(cpu, "r1  : %08lx, r0 : %08lx\n",
			      ((struct pt_regs *)regs_ptr)->ARM_r1,
			      ((struct pt_regs *)regs_ptr)->ARM_r0);
	return;
}

/* dump the stack into buffer */
static void aee_wdt_dump_stack_bin(unsigned int cpu, unsigned long bottom, unsigned long top)
{
	int i, count = 0;
	unsigned long p, fp;
	unsigned long high;
	struct stackframe cur_frame;

	stacks_buffer_bin[cpu].real_len =
	    aee_dump_stack_top_binary(stacks_buffer_bin[cpu].bin_buf,
				      sizeof(stacks_buffer_bin[cpu].bin_buf), bottom, top);
	stacks_buffer_bin[cpu].top = top;
	stacks_buffer_bin[cpu].bottom = bottom;

	/* should check stack address in kernel range */
	if (bottom & 3) {
		aee_wdt_percpu_printf(cpu, "%s bottom unaligned %08lx\n", __func__, bottom);
		return;
	}
	if (!((bottom >= (PAGE_OFFSET + THREAD_SIZE)) && virt_addr_valid(bottom))) {
		aee_wdt_percpu_printf(cpu, "%s bottom out of kernel addr space %08lx\n", __func__,
				      bottom);
		return;
	}
	if (!((top >= (PAGE_OFFSET + THREAD_SIZE)) && virt_addr_valid(bottom))) {
		aee_wdt_percpu_printf(cpu, "%s top out of kernel addr space %08lx\n", __func__,
				      top);
		return;
	}

	aee_wdt_percpu_printf(cpu, "stack (0x%08lx to 0x%08lx)\n", bottom, top);
	for (p = bottom; p < top; p += 4) {
		unsigned long val;
		if (count == 0)
			aee_wdt_percpu_printf(cpu, "%04lx: ", p & 0xffff);
		val = *((unsigned long *)(p));
		aee_wdt_percpu_printf(cpu, "%08lx ", val);

		count++;
		if (count == 8) {
			aee_wdt_percpu_printf(cpu, "\n");
			count = 0;
		}
	}

	/* save backtrace addr */
	high = ALIGN(bottom, THREAD_SIZE);
	/* cur_frame.pc = regs_buffer_bin[cpu].regs.ARM_pc; */
	cur_frame.lr = regs_buffer_bin[cpu].regs.ARM_pc;
	cur_frame.fp = regs_buffer_bin[cpu].regs.ARM_fp;
	/* cur_frame.sp = regs_buffer_bin[cpu].regs.ARM_sp; */
	for (i = 0; i < MAX_EXCEPTION_FRAME; i++) {
		wdt_percpu_stackframe[cpu][i] = cur_frame.lr;
		fp = cur_frame.fp;
		if ((fp < (bottom + 12)) || ((fp + 4) >= high)) {
			/* aee_wdt_percpu_printf(cpu, "\n fp %08lx invalid\n", fp); */
			return;
		}
		cur_frame.fp = *(unsigned long *)(fp - 12);
		/* cur_frame.sp = *(unsigned long *)(fp - 8); */
		cur_frame.lr = *(unsigned long *)(fp - 4);
		/* cur_frame.pc = *(unsigned long *)(fp); */
		if (!((cur_frame.lr >= (PAGE_OFFSET + THREAD_SIZE)) &&
		      virt_addr_valid(cur_frame.lr))) {
			/* aee_wdt_percpu_printf(cpu, "\n lr %08lx invalid\n", cur_frame.lr); */
			return;
		}
	}

	return;
}
#endif				/* #ifdef CONFIG_FIQ_GLUE */

/* save binary register and stack value into ram console */
static void aee_save_reg_stack_sram(void)
{
	int cpu, i;
	char str_buf[256];
	int len = 0;

	for (cpu = 0; cpu < num_possible_cpus(); cpu++) {
		if (regs_buffer_bin[cpu].real_len != 0) {
			snprintf(str_buf, sizeof(str_buf),
				 "\n\ncpu %d preempt=%lx, softirq=%lx, hardirq=%lx ", cpu,
				 ((wdt_percpu_preempt_cnt[cpu] & PREEMPT_MASK) >> PREEMPT_SHIFT),
				 ((wdt_percpu_preempt_cnt[cpu] & SOFTIRQ_MASK) >> SOFTIRQ_SHIFT),
				 ((wdt_percpu_preempt_cnt[cpu] & HARDIRQ_MASK) >> HARDIRQ_SHIFT));
			aee_sram_fiq_log(str_buf);

			memset(str_buf, 0, sizeof(str_buf));
			snprintf(str_buf, sizeof(str_buf),
				 "\ncpu %d r0->r10 fp ip sp lr pc cpsr orig_r0\n", cpu);
			aee_sram_fiq_log(str_buf);
			aee_sram_fiq_save_bin((char *)&(regs_buffer_bin[cpu].regs),
					      regs_buffer_bin[cpu].real_len);
		}

		if (stacks_buffer_bin[cpu].real_len > 0) {
			memset(str_buf, 0, sizeof(str_buf));
			snprintf(str_buf, sizeof(str_buf), "\ncpu %d stack [%08lx %08lx]\n",
				 cpu, stacks_buffer_bin[cpu].bottom, stacks_buffer_bin[cpu].top);
			aee_sram_fiq_log(str_buf);
			aee_sram_fiq_save_bin(stacks_buffer_bin[cpu].bin_buf,
					      stacks_buffer_bin[cpu].real_len);

			memset(str_buf, 0, sizeof(str_buf));
			len = snprintf(str_buf, sizeof(str_buf), "\ncpu %d backtrace : ", cpu);
			aee_sram_fiq_log(str_buf);
			for (i = 0; i < MAX_EXCEPTION_FRAME; i++) {
				if (wdt_percpu_stackframe[cpu][i] == 0)
					break;
				snprintf(str_buf, sizeof(str_buf),
						"<%08lx> %pS, ",
						wdt_percpu_stackframe[cpu][i],
						(void *) wdt_percpu_stackframe[cpu][i]);
				aee_sram_fiq_log(str_buf);
			}
		}
	}
	aee_sram_fiq_log("\n\n");
}

#ifdef CONFIG_SMP
#ifdef CONFIG_FIQ_GLUE
void aee_fiq_ipi_cpu_stop(void *arg, void *regs, void *svc_sp)
{
	int cpu = 0;
	register int sp asm("sp");
	struct pt_regs *ptregs = (struct pt_regs *)regs;

	asm volatile("mov %0, %1\n\t"
			"mov fp, %2\n\t"
			: "=r" (sp)
			: "r"(svc_sp), "r"(ptregs->ARM_fp)
			);

	cpu = get_HW_cpuid();
	aee_wdt_percpu_printf(cpu, "CPU%u: stopping by FIQ\n", cpu);
	wdt_percpu_preempt_cnt[cpu] = preempt_count();
	aee_wdt_percpu_printf(cpu, "preempt=%lx, softirq=%lx, hardirq=%lx\n",
			      ((wdt_percpu_preempt_cnt[cpu] & PREEMPT_MASK) >> PREEMPT_SHIFT),
			      ((wdt_percpu_preempt_cnt[cpu] & SOFTIRQ_MASK) >> SOFTIRQ_SHIFT),
			      ((wdt_percpu_preempt_cnt[cpu] & HARDIRQ_MASK) >> HARDIRQ_SHIFT));
	aee_dump_cpu_reg_bin(cpu, regs);
	aee_wdt_dump_stack_bin(cpu, ((struct pt_regs *)regs)->ARM_sp,
			       ((struct pt_regs *)regs)->ARM_sp + WDT_SAVE_STACK_SIZE);

	set_cpu_online(cpu, false);
	local_fiq_disable();
	local_irq_disable();

	while (1)
		cpu_relax();
}

void aee_smp_send_stop(void)
{
	unsigned long timeout;
	struct cpumask mask;
	int cpu = 0;

	cpumask_copy(&mask, cpu_online_mask);
	cpu = get_HW_cpuid();
	cpumask_clear_cpu(cpu, &mask);
	/* mt_fiq_printf("\n fiq_smp_call_function\n"); */
	fiq_smp_call_function(aee_fiq_ipi_cpu_stop, NULL, 0);

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while (num_online_cpus() > 1 && timeout--)
		udelay(1);

	if (num_online_cpus() > 1)
		aee_wdt_printf("WDT: failed to stop other CPUs in FIQ\n");
}
#else

void aee_smp_send_stop(void)
{
	unsigned long timeout;
	struct cpumask mask;
	int cpu = 0;

	cpumask_copy(&mask, cpu_online_mask);
	cpu = get_HW_cpuid();
	cpumask_clear_cpu(cpu, &mask);
	arch_send_cpu_stop_ipi_mask(&mask);

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while (num_online_cpus() > 1 && timeout--)
		udelay(1);

	if (num_online_cpus() > 1)
		aee_wdt_printf("WDT: failed to stop other CPUs\n");
}
#endif				/* #ifdef CONFIG_FIQ_GLUE */
#endif				/* #ifdef CONFIG_SMP */

void aee_wdt_irq_info(void)
{
	unsigned long long t;
	unsigned long nanosec_rem;
	int res = 0;
	struct wd_api *wd_api = NULL;

	res = get_wd_api(&wd_api);

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_KICK);
	if (res)
		aee_wdt_printf("aee_wdt_irq_info, get wd api error\n");
	else
		wd_api->wd_restart(WD_TYPE_NOLOCK);

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_SMP_STOP);
#ifdef CONFIG_SMP
	aee_smp_send_stop();
#endif

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_STACK);
	aee_save_reg_stack_sram();

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_TIME);
	t = cpu_clock(smp_processor_id());
	nanosec_rem = do_div(t, 1000000000);
	aee_wdt_printf("\nQwdt at [%5lu.%06lu] on CPU%d\n", (unsigned long)t, nanosec_rem / 1000, get_HW_cpuid());

#ifdef WDT_DEBUG_VERBOSE
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_GIC);
	mt_irq_dump();

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_LOCALTIMER);
	wdt_log_length +=
	    dump_localtimer_info((wdt_log_buf + wdt_log_length),
				 (sizeof(wdt_log_buf) - wdt_log_length));

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_IDLE);
	wdt_log_length +=
	    dump_idle_info((wdt_log_buf + wdt_log_length), (sizeof(wdt_log_buf) - wdt_log_length));
#endif

#ifdef CONFIG_MT_SCHED_MONITOR
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_SCHED);
	mt_aee_dump_sched_traces();
#endif
	aee_sram_fiq_log(wdt_log_buf);

	/* avoid lock prove to dump_stack in __debug_locks_off() */
	xchg(&debug_locks, 0);
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_IRQ_DONE);

	++oops_in_progress;

#ifdef CONFIG_MDUMP
	mdump_mark_reboot_reason(MDUMP_REBOOT_WATCHDOG);
#endif
	rtc_mark_reboot_reason(RTC_REBOOT_REASON_SW_WDT);

	BUG();
}

#ifdef CONFIG_MT_SCHED_MONITOR
void dump_sched_traces(void)
{
	struct pt_regs *regs = get_irq_regs();
	int cpu;

	cpu = get_HW_cpuid();

	aee_wdt_printf("Soft lockup on CPU %d\n", cpu);
	aee_sram_fiq_log(wdt_log_buf);
	wdt_log_length = 0;

	wdt_percpu_preempt_cnt[cpu] = preempt_count();
	aee_dump_cpu_reg_bin(cpu, regs);
	aee_wdt_dump_stack_bin(cpu, regs->ARM_sp, regs->ARM_sp + WDT_SAVE_STACK_SIZE);
	aee_save_reg_stack_sram();

	mt_aee_dump_sched_traces();
	aee_sram_fiq_log(wdt_log_buf);
	wdt_log_length = 0;
	wdt_percpu_log_length[cpu] = 0;
}
#endif

#if defined(CONFIG_FIQ_GLUE)

void aee_wdt_fiq_info(void *arg, void *regs, void *svc_sp)
{
	register int sp asm("sp");
	struct pt_regs *ptregs = (struct pt_regs *)regs;
	int cpu = 0;

	asm volatile("mov %0, %1\n\t"
		"mov fp, %2\n\t"
		: "=r" (sp)
		: "r"(svc_sp), "r"(ptregs->ARM_fp)
		);

	cpu = get_HW_cpuid();
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_FIQ_INFO);
	/* mt_fiq_printf("\n Triggered :cpu-%d\n", cpu); */
	aee_wdt_percpu_printf(cpu, "CPU %d FIQ: Watchdog time out\n", cpu);
	wdt_percpu_preempt_cnt[cpu] = preempt_count();
	aee_wdt_percpu_printf(cpu, "preempt=%lx, softirq=%lx, hardirq=%lx\n",
			      ((wdt_percpu_preempt_cnt[cpu] & PREEMPT_MASK) >> PREEMPT_SHIFT),
			      ((wdt_percpu_preempt_cnt[cpu] & SOFTIRQ_MASK) >> SOFTIRQ_SHIFT),
			      ((wdt_percpu_preempt_cnt[cpu] & HARDIRQ_MASK) >> HARDIRQ_SHIFT));

	aee_dump_cpu_reg_bin(cpu, regs);
	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_FIQ_STACK);
	aee_wdt_dump_stack_bin(cpu, ((struct pt_regs *)regs)->ARM_sp,
			       ((struct pt_regs *)regs)->ARM_sp + WDT_SAVE_STACK_SIZE);

	if (atomic_xchg(&wdt_enter_fiq, 1) != 0) {
		aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_FIQ_LOOP);
		aee_wdt_percpu_printf(cpu, "Other CPU already enter WDT FIQ handler\n");
		/* loop forever here to avoid SMP deadlock risk during panic flow */
		do { } while (1);
	}

	aee_rr_rec_fiq_step(AEE_FIQ_STEP_WDT_FIQ_DONE);
	aee_wdt_irq_info();
}
#endif				/* CONFIG_FIQ_GLUE */

static int __init aee_wdt_init(void)
{
	int i;
	atomic_set(&wdt_enter_fiq, 0);
	for (i = 0; i < num_possible_cpus(); i++) {
		wdt_percpu_log_buf[i] = kzalloc(WDT_PERCPU_LOG_SIZE, GFP_KERNEL);
		if (wdt_percpu_log_buf[i] == NULL)
			pr_err("\n aee_common_init : kmalloc fail\n");

		wdt_percpu_log_length[i] = 0;
		wdt_percpu_preempt_cnt[i] = 0;
	}
	memset(wdt_log_buf, 0, sizeof(wdt_log_buf));
	memset(regs_buffer_bin, 0, sizeof(regs_buffer_bin));
	memset(stacks_buffer_bin, 0, sizeof(stacks_buffer_bin));
	memset(wdt_percpu_stackframe, 0, sizeof(wdt_percpu_stackframe));
	return 0;
}
module_init(aee_wdt_init);
