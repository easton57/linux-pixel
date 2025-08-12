# SPDX-License-Identifier: GPL-2.0

subdir-ccflags-y += \
		-I$(srctree)/drivers/soc/gs/include \
		-I$(srctree)/drivers/soc/gs/include/uapi \

# obj-y += ../../phy/

# obj-y += ../../pinctrl/

ifeq ($(CONFIG_SOC_ZUMA),y)
obj-y += ../../pci/controller/dwc/
else
obj-y += ../../pci/controller/dwc-whi/
endif

obj-y += ../../clk/gs/

# obj-y += ../../dma/

obj-y += ../google/

# obj-y += ../../regulator/

# obj-y += ../../tty/serial/

# obj-y += ../../char/hw_random/

# obj-y += ../../iommu/

# obj-y += ../../gpu/

# obj-y += ../../misc/

# obj-y += ../../mfd/

# obj-y += ../../dma-buf/heaps/

# obj-y += ../../ufs/

# obj-y += ../../spi/

obj-y += ../../spmi/

# obj-y += ../../usb/

# obj-y += ../../input/

# obj-y += ../../rtc/

obj-y += ../../i2c/busses/

obj-y += ../../media/platform/

# obj-y += ../../power/

# obj-y += ../../thermal/

# obj-y += ../../watchdog/

# obj-y += ../../cpufreq/

# obj-y += ../../clocksource/

ifeq ($(CONFIG_SOC_ZUMA),y)
obj-y += ../../devfreq/google/
else
obj-y += ../../devfreq-whi/google/
endif

obj-y += ../../performance/gs_perf_mon/

obj-y += ../../performance/lat_governors/

# obj-y += ../../iio/

# obj-y += ../../bts/

# obj-y += ../../block/zram/

# obj-y += ../../pwm/

# obj-y += ../../video/backlight/
