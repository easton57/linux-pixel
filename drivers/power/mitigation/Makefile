# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_GOOGLE_BCL) += google_bcl.o

google_bcl-y			+= google_bcl_core.o
google_bcl-y			+= google_bcl_irq_mon.o
google_bcl-y			+= google_bcl_sysfs.o
google_bcl-y			+= google_bcl_util.o
google_bcl-y			+= google_bcl_qos.o
google_bcl-y			+= google_bcl_debugfs.o
google_bcl-y			+= max77759_vdroop.o
google_bcl-y			+= max77779_vdroop.o
google_bcl-y			+= google_bcl_data_logging.o

# Conditionally compile this object if either of these regulators are enabled
google_bcl-$(CONFIG_REGULATOR_S2MPG12) += google_bcl_votable.o
google_bcl-$(CONFIG_REGULATOR_S2MPG14) += google_bcl_votable.o
