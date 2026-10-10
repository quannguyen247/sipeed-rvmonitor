# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 Nguyen Dong Quan
OSDRV_DIR ?=
ccflags-y += -D__CV181X__ -I$(src)/common -I$(src)/hal/cv181x
ccflags-y += -I$(OSDRV_DIR)/interdrv/include/uapi
ccflags-y += -I$(OSDRV_DIR)/interdrv/include/chip/cv181x/uapi/linux
ccflags-y += -I$(srctree)/drivers/tee -I$(srctree)/drivers/staging/android/ion
obj-m += soph_tpu.o
soph_tpu-y := common/cvi_tpu_interface.o hal/cv181x/tpu_platform.o hal/cv181x/tpu_pmu.o
