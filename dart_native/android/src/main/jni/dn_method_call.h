//
// Created by Hui on 5/31/21.
//
#ifndef DART_NATIVE_DN_METHOD_CALL_H
#define DART_NATIVE_DN_METHOD_CALL_H

#include <stdint.h>
#include <jni.h>
#include <map>
#include <string>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void *CallNativeMethod(JNIEnv *, jobject, jmethodID, jvalue *);

    void *callNativeCharMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeIntMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeDoubleMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeFloatMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeByteMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeShortMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeLongMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeBooleanMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);
    void *callNativeVoidMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);

    void *callNativeStringMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments);

    const std::map<char, std::function<CallNativeMethod> > methodCallerMap = {
        {'C', callNativeCharMethod},
        {'I', callNativeIntMethod},
        {'D', callNativeDoubleMethod},
        {'F', callNativeFloatMethod},
        {'B', callNativeByteMethod},
        {'S', callNativeShortMethod},
        {'J', callNativeLongMethod},
        {'Z', callNativeBooleanMethod},
        {'V', callNativeVoidMethod}};

#ifdef __cplusplus
}
#endif
#endif //DART_NATIVE_DN_METHOD_CALL_H
