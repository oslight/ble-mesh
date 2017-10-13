ccflags-y +=-I${ZEPHYR_BASE}/net/ip
ccflags-y +=-I${ZEPHYR_BASE}/tests/include
ccflags-y +=-I${SOURCE_DIR}/lib

obj-y = main.o
obj-y += bluetooth.o
obj-y += app_work_queue.o
obj-$(CONFIG_SYS_LOG_EXT_HOOK) += tstamp_log.o

# Library code for FOTA and other generic support services.
obj-y += lib/
