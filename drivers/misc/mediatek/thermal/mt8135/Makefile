ccflags-y += -I$(srctree) -Werror
obj-  := dummy.o

ifneq ($(CONFIG_MTK_COMBO_WIFI),)
    obj-$(CONFIG_THERMAL) += mtk_ts_wmt.o
endif

ifeq ($(CONFIG_THERMAL),y)
    obj-y += mtk_cpu_adaptive_cooler.o
    obj-y += mtk_thermal_budget.o
    obj-y += mtk_ts_cpu.o
    obj-y += mtk_ts_pmic.o
endif
