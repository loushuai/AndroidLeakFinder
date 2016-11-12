LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := memleak
LOCAL_SRC_FILES += backtrace.c \
				   stats.c \
					 stackfilter.c \
				   memleakfinder.c \
				   fdleakfinder.c


LOCAL_CFLAGS += -std=gnu99
LOCAL_LDLIBS += -llog

include $(BUILD_SHARED_LIBRARY)
