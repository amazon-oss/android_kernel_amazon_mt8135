#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <asm/setup.h>
#include <mach/mt_boot.h>

static char new_command_line[COMMAND_LINE_SIZE];

static void append_to_cmdline(char *cmdline, const char *param)
{
    size_t len = strlen(cmdline);
    size_t param_len = strlen(param);

    if (len + param_len + 2 <= COMMAND_LINE_SIZE) {
        if (len > 0 && cmdline[len - 1] != ' ') {
            strcat(cmdline, " ");
            len++;
        }
        strlcpy(cmdline + len, param, COMMAND_LINE_SIZE - len);
    } else {
        printk(KERN_WARNING "cmdline: Not enough space to append '%s'\n", param);
    }
}

static void patch_charger_flags(char *cmdline)
{
    if (g_boot_reason == BR_USB) {
        append_to_cmdline(cmdline, "androidboot.mode=charger");
    }
}

static int cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", new_command_line);
	return 0;
}

static int cmdline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cmdline_proc_show, NULL);
}

static const struct file_operations cmdline_proc_fops = {
	.open		= cmdline_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_cmdline_init(void)
{
    strlcpy(new_command_line, saved_command_line, sizeof(new_command_line));

    /*
     * Android automatically converts 'androidboot.*' properties to
     * 'ro.boot.*' properties, since Amazon doesn't natively do this
     * conversion, we do it here instead.
     */
    patch_charger_flags(new_command_line);
    proc_create("cmdline", 0, NULL, &cmdline_proc_fops);

    return 0;
}

module_init(proc_cmdline_init);
