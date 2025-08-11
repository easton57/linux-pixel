# SPDX-License-Identifier: GPL-2.0
#
# Makefile for GCMA

obj-$(CONFIG_GCMA) += gcma.o

gcma-y                    := gcma_core.o gcma_sysfs.o
gcma-$(CONFIG_ANDROID_VENDOR_HOOKS) += gcma_vh.o
gcma-$(CONFIG_DEBUG_FS) += gcma_debug.o
