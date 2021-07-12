//
// Created by Hui on 5/31/21.
//
#include "dn_method_call.h"
#include "dn_type_convert.h"

void *callNativeCharMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto nativeChar = env->CallCharMethodA(object, methodId, arguments);
  return (void *)nativeChar;
}

void *callNativeIntMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto nativeInt = env->CallIntMethodA(object, methodId, arguments);
  return (void *)nativeInt;
}

void *callNativeDoubleMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  void *callResult;

  auto nativeDouble = env->CallDoubleMethodA(object, methodId, arguments);
  auto templeD = (double)nativeDouble;

  memcpy(&callResult, &templeD, sizeof(double));
  return callResult;
}

void *callNativeFloatMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  void *callResult;

  auto nativeDouble = env->CallFloatMethodA(object, methodId, arguments);
  auto templeF = (float)nativeDouble;

  memcpy(&callResult, &templeF, sizeof(float));
  return callResult;
}

void *callNativeByteMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto nativeByte = env->CallByteMethodA(object, methodId, arguments);
  return (void *)nativeByte;
}

void *callNativeShortMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto nativeShort = env->CallShortMethodA(object, methodId, arguments);
  return (void *)nativeShort;
}

void *callNativeLongMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto nativeLong = env->CallLongMethodA(object, methodId, arguments);
  return (void *)nativeLong;
}

void *callNativeBooleanMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto nativeBool = env->CallBooleanMethodA(object, methodId, arguments);
  return (void *)nativeBool;
}

void *callNativeVoidMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  env->CallVoidMethodA(object, methodId, arguments);
  return nullptr;
}

void *callNativeStringMethod(JNIEnv *env, jobject object, jmethodID methodId, jvalue *arguments)
{
  auto javaString = (jstring)env->CallObjectMethodA(object, methodId, arguments);
  return convertToDartUtf16(env, javaString);
}
