EFIWRAPPER_LOCAL_PATH := $(call my-dir)
EFIWRAPPER_CFLAGS := -Wall -Wextra -Werror

ifeq ($(TARGET_BUILD_VARIANT),user)
    EFIWRAPPER_CFLAGS += -DUSER -DUSERDEBUG
endif

ifeq ($(TARGET_BUILD_VARIANT),userdebug)
    EFIWRAPPER_CFLAGS += -DUSERDEBUG
endif

EFIWRAPPER_CFLAGS += -DPRODUCT_NAME=\"$(TARGET_PRODUCT)\"

ifeq ($(PRODUCT_MANUFACTURER),)
    EFIWRAPPER_CFLAGS += -DPRODUCT_MANUFACTURER=\"unknown\"
else
    EFIWRAPPER_CFLAGS += -DPRODUCT_MANUFACTURER=\"$(PRODUCT_MANUFACTURER)\"
endif

ifeq ($(KERNELFLINGER_DISABLE_DEBUG_PRINT),true)
    EFIWRAPPER_CFLAGS += -DDISABLE_DEBUG_PRINT
endif

EFIWRAPPER_HOST_CFLAGS := \
	$(EFIWRAPPER_CFLAGS) \
	-fshort-wchar \
	-DEFI_FUNCTION_WRAPPER \
	-DGNU_EFI_USE_MS_ABI \
	-DHOST

ifeq ($(TARGET_IAFW_ARCH),x86_64)
    EFIWRAPPER_HOST_ARCH += x86_64
else
    EFIWRAPPER_HOST_ARCH += x86
endif

EFIWRAPPER_HOST_C_INCLUDES := \
	external/gnu-efi/gnu-efi-3.0/inc \
	external/gnu-efi/gnu-efi-3.0/inc/$(TARGET_EFI_ARCH_NAME) \
	external/gnu-efi/gnu-efi-3.0/inc/protocol

include $(call all-subdir-makefiles)

include $(CLEAR_VARS)
LOCAL_PATH := $(EFIWRAPPER_LOCAL_PATH)
LOCAL_MODULE := efiwrapper-$(TARGET_BUILD_VARIANT)
LOCAL_STATIC_LIBRARIES := \
	libefiwrapper-$(TARGET_BUILD_VARIANT) \
	libgnuefi \
	libefi \
	libefiwrapper_drivers-$(TARGET_BUILD_VARIANT)
LOCAL_CFLAGS := $(EFIWRAPPER_CFLAGS)
LOCAL_SRC_FILES := \
	main.c
include $(BUILD_IAFW_STATIC_LIBRARY)
