LOCAL_PATH := $(call my-dir)


###########################
#
# FFmpeg shared library
#
###########################

include $(CLEAR_VARS)

LOCAL_MODULE:= avcodec

LOCAL_SRC_FILES:= lib/libavcodec-57.so

LOCAL_EXPORT_C_INCLUDES:= $(LOCAL_PATH)/include

include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE:= avformat

LOCAL_SRC_FILES:= lib/libavformat-57.so

LOCAL_EXPORT_C_INCLUDES:= $(LOCAL_PATH)/include

include $(PREBUILT_SHARED_LIBRARY)



include $(CLEAR_VARS)

LOCAL_MODULE:= swscale

LOCAL_SRC_FILES:= lib/libswscale-4.so

LOCAL_EXPORT_C_INCLUDES:= $(LOCAL_PATH)/include

include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE:= avutil

LOCAL_SRC_FILES:= lib/libavutil-55.so

LOCAL_EXPORT_C_INCLUDES:= $(LOCAL_PATH)/include

include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE:= avfilter

LOCAL_SRC_FILES:= lib/libavfilter-6.so

LOCAL_EXPORT_C_INCLUDES:= $(LOCAL_PATH)/include

include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE:= swresample

LOCAL_SRC_FILES:= lib/libswresample-2.so

LOCAL_EXPORT_C_INCLUDES:= $(LOCAL_PATH)/include

include $(PREBUILT_SHARED_LIBRARY)

###########################
#
# SDL shared library
#
###########################

include $(CLEAR_VARS)

LOCAL_MODULE := SDL2

LOCAL_ARM_MODE=arm

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include \

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := \
	$(subst $(LOCAL_PATH)/,, \
	$(wildcard $(LOCAL_PATH)/src/*.c) \
	$(wildcard $(LOCAL_PATH)/src/audio/*.c) \
	$(wildcard $(LOCAL_PATH)/src/audio/android/*.c) \
	$(wildcard $(LOCAL_PATH)/src/audio/dummy/*.c) \
	$(LOCAL_PATH)/src/atomic/SDL_atomic.c \
	$(LOCAL_PATH)/src/atomic/SDL_spinlock.c.arm \
	$(wildcard $(LOCAL_PATH)/src/core/android/*.c) \
	$(wildcard $(LOCAL_PATH)/src/cpuinfo/*.c) \
	$(wildcard $(LOCAL_PATH)/src/dynapi/*.c) \
	$(wildcard $(LOCAL_PATH)/src/events/*.c) \
	$(wildcard $(LOCAL_PATH)/src/file/*.c) \
	$(wildcard $(LOCAL_PATH)/src/haptic/*.c) \
	$(wildcard $(LOCAL_PATH)/src/haptic/dummy/*.c) \
	$(wildcard $(LOCAL_PATH)/src/joystick/*.c) \
	$(wildcard $(LOCAL_PATH)/src/joystick/android/*.c) \
	$(wildcard $(LOCAL_PATH)/src/loadso/dlopen/*.c) \
	$(wildcard $(LOCAL_PATH)/src/power/*.c) \
	$(wildcard $(LOCAL_PATH)/src/power/android/*.c) \
	$(wildcard $(LOCAL_PATH)/src/filesystem/android/*.c) \
	$(wildcard $(LOCAL_PATH)/src/render/*.c) \
	$(wildcard $(LOCAL_PATH)/src/render/*/*.c) \
	$(wildcard $(LOCAL_PATH)/src/stdlib/*.c) \
	$(wildcard $(LOCAL_PATH)/src/thread/*.c) \
	$(wildcard $(LOCAL_PATH)/src/thread/pthread/*.c) \
	$(wildcard $(LOCAL_PATH)/src/timer/*.c) \
	$(wildcard $(LOCAL_PATH)/src/timer/unix/*.c) \
	$(wildcard $(LOCAL_PATH)/src/video/*.c) \
	$(wildcard $(LOCAL_PATH)/src/video/android/*.c) \
	$(wildcard $(LOCAL_PATH)/src/test/*.c))

LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES
LOCAL_LDLIBS := -ldl -lGLESv1_CM -lGLESv2 -llog -landroid

include $(BUILD_SHARED_LIBRARY)

###########################
#
# SDL static library
#
###########################

LOCAL_MODULE := SDL2_static

LOCAL_MODULE_FILENAME := libSDL2

LOCAL_SRC_FILES += $(LOCAL_PATH)/src/main/android/SDL_android_main.c

LOCAL_LDLIBS :=
LOCAL_EXPORT_LDLIBS := -Wl,--undefined=Java_com_righere_convexdplayer_sdl_SDLActivity_nativeInit -ldl -lGLESv1_CM -lGLESv2 -llog -landroid

include $(BUILD_STATIC_LIBRARY)

###########################
#
# SDL My Custom library
# add by ldq 20161122
#
###########################

include $(CLEAR_VARS)

LOCAL_MODULE := SDL2main

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include \

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

# Add your application source files here...

LOCAL_SRC_FILES := $(LOCAL_PATH)/src/main/android/SDL_android_main.c \
	$(LOCAL_PATH)/convexd_native_render.c

LOCAL_SHARED_LIBRARIES := SDL2 avcodec avfilter avformat avutil  swresample swscale

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -llog

include $(BUILD_SHARED_LIBRARY)