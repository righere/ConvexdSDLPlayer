#include <jni.h>
#include <string>

#include "include/libavformat/avformat.h"

extern "C"
jstring
Java_com_righere_convexdplayer_MainActivity_stringFromJNI(
JNIEnv *env,
jobject /* this */) {
std::string hello = "Hello from C++";
return env->NewStringUTF(hello.c_str());
}
