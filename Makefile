BOARD ?= 96b_nitrogen
CONF_FILE ?= prj.conf
CONF_FILE += $(wildcard local.conf) \
		$(wildcard boards/$(BOARD).conf) \
		$(wildcard boards/$(BOARD)-local.conf)
DTC_OVERLAY_DIR := $(CURDIR)/boards

KBUILD_KCONFIG = $(CURDIR)/Kconfig
export KBUILD_KCONFIG
export DTC_OVERLAY_DIR

include $(ZEPHYR_BASE)/Makefile.inc
