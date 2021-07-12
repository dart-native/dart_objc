#include <jni.h>
#include <map>
#include <string>
#include <regex>
#include <dart_api_dl.h>
#include <semaphore.h>
#include "dn_type_convert.h"
#include "dn_log.h"
#include "dn_method_call.h"
#include "dn_signature_helper.h"
#include "dn_callback.h"
#include "jni_object_ref.h"

extern "C"
{

  static JavaVM *gJvm = nullptr;
  static JavaGlobalRef<jobject> *gClassLoader = nullptr;
  static jmethodID gFindClassMethod;
  static pthread_key_t detachKey = 0;

  /// for invoke result compare
  static JavaGlobalRef<jclass> *gStrCls = nullptr;

  /// key is jobject, value is pair which contain jclass and reference count
  static std::map<jobject, std::pair<jclass, int> > objectGlobalReference;

  /// protect objectGlobalReference
  std::mutex globalReferenceMtx;

  void _addGlobalObject(jobject globalObject, jclass globalClass)
  {
    std::lock_guard<std::mutex> lockGuard(globalReferenceMtx);
    std::pair<jclass, int> objPair = std::make_pair(globalClass, 0);
    objectGlobalReference[globalObject] = objPair;
  }

  jclass _getGlobalClass(jobject globalObject)
  {
    std::lock_guard<std::mutex> lockGuard(globalReferenceMtx);
    auto it = objectGlobalReference.find(globalObject);
    auto cls = it != objectGlobalReference.end() ? it->second.first : nullptr;
    return cls;
  }

  void _detachThreadDestructor(void *arg)
  {
    DNDebug("detach from current thread");
    gJvm->DetachCurrentThread();
    detachKey = 0;
  }

  /// each thread attach once
  JNIEnv *_getEnv()
  {
    if (gJvm == nullptr)
    {
      return nullptr;
    }

    if (detachKey == 0)
    {
      pthread_key_create(&detachKey, _detachThreadDestructor);
    }
    JNIEnv *env;
    jint ret = gJvm->GetEnv((void **)&env, JNI_VERSION_1_6);

    switch (ret)
    {
    case JNI_OK:
      return env;
    case JNI_EDETACHED:
      DNDebug("attach to current thread");
      gJvm->AttachCurrentThread(&env, nullptr);
      return env;
    default:
      DNDebug("fail to get env");
      return nullptr;
    }
  }

  JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *pjvm, void *reserved)
  {
    DNDebug("JNI_OnLoad");
    /// cache the JavaVM pointer
    gJvm = pjvm;
    JNIEnv *env = _getEnv();

    /// cache classLoader
    auto plugin = env->FindClass("com/dartnative/dart_native/DartNativePlugin");
    jclass pluginClass = env->GetObjectClass(plugin);
    auto classLoaderClass = env->FindClass("java/lang/ClassLoader");
    auto getClassLoaderMethod = env->GetMethodID(pluginClass, "getClassLoader",
                                                 "()Ljava/lang/ClassLoader;");
    auto classLoader = env->CallObjectMethod(plugin, getClassLoaderMethod);
    gClassLoader = new JavaGlobalRef<jobject>(env->NewGlobalRef(classLoader), env);
    gFindClassMethod = env->GetMethodID(classLoaderClass, "findClass",
                                        "(Ljava/lang/String;)Ljava/lang/Class;");

    /// cache string class
    jclass strCls = env->FindClass("java/lang/String");
    gStrCls = new JavaGlobalRef<jclass>(static_cast<jclass>(env->NewGlobalRef(strCls)), env);

    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(plugin);
    env->DeleteLocalRef(pluginClass);
    env->DeleteLocalRef(classLoaderClass);
    env->DeleteLocalRef(strCls);
    DNDebug("JNI_OnLoad finish");
    return JNI_VERSION_1_6;
  }

  jclass _findClass(JNIEnv *env, const char *name)
  {
    jclass nativeClass = nullptr;
    nativeClass = env->FindClass(name);
    jthrowable exception = env->ExceptionOccurred();
    /// class loader not found class
    if (exception)
    {
      env->ExceptionClear();
      DNDebug("findClass exception");
      return static_cast<jclass>(env->CallObjectMethod(gClassLoader->Object(),
                                                       gFindClassMethod,
                                                       env->NewStringUTF(name)));
    }
    return nativeClass;
  }

  /// fill all arguments to jvalues
  /// when argument is string the stringTypeBitmask's bit is 1
  void _fillArgs(void **arguments, char **argumentTypes, jvalue *argValues, int argumentCount, uint32_t stringTypeBitmask)
  {
    JNIEnv *env = _getEnv();
    for (jsize index(0); index < argumentCount; ++arguments, ++index)
    {
      /// check basic map convert
      auto it = basicTypeConvertMap.find(*argumentTypes[index]);

      if (it == basicTypeConvertMap.end())
      {
        /// when argument type is string or stringTypeBitmask mark as string
        if (strcmp(argumentTypes[index], "Ljava/lang/String;") == 0 || (stringTypeBitmask >> index & 0x1) == 1)
        {
          convertToJavaUtf16(env, *arguments, argValues, index);
        }
        else
        {
          /// convert from object cache
          /// check callback cache, if true using proxy object
          jobject object = getNativeCallbackProxyObject(*arguments);
          argValues[index].l = object == nullptr ? static_cast<jobject>(*arguments) : object;
        }
      }
      else
      {
        it->second(arguments, argValues, index);
      }
    }
  }

  jobject _newObject(jclass cls, void **arguments, char **argumentTypes, int argumentCount, uint32_t stringTypeBitmask)
  {
    auto *argValues = new jvalue[argumentCount];
    JNIEnv *env = _getEnv();
    if (argumentCount > 0)
    {
      _fillArgs(arguments, argumentTypes, argValues, argumentCount, stringTypeBitmask);
    }

    char *constructorSignature = generateSignature(argumentTypes, argumentCount, const_cast<char *>("V"));
    jmethodID constructor = env->GetMethodID(cls, "<init>", constructorSignature);
    jobject newObj = env->NewObjectA(cls, constructor, argValues);

    delete[] argValues;
    free(constructorSignature);
    return newObj;
  }

  /// create target object
  void *createTargetObject(char *targetClassName, void **arguments, char **argumentTypes, int argumentCount, uint32_t stringTypeBitmask)
  {
    JNIEnv *env = _getEnv();
    jclass cls = _findClass(env, targetClassName);
    jobject newObj = _newObject(cls, arguments, argumentTypes, argumentCount, stringTypeBitmask);
    jobject gObj = env->NewGlobalRef(newObj);
    _addGlobalObject(gObj, static_cast<jclass>(env->NewGlobalRef(cls)));

    env->DeleteLocalRef(newObj);
    env->DeleteLocalRef(cls);
    return gObj;
  }

  /// invoke native method
  void *invokeNativeMethod(void *objPtr, char *methodName, void **arguments, char **dataTypes, int argumentCount, char *returnType, uint32_t stringTypeBitmask)
  {
    auto object = static_cast<jobject>(objPtr);
    jclass cls = _getGlobalClass(object);
    if (cls == nullptr)
    {
      /// maybe use cache pointer but jobject is release
      DNError("invokeNativeMethod not find class, check pointer and jobject lifecycle is same");
      return nullptr;
    }

    void *nativeInvokeResult = nullptr;
    JNIEnv *env = _getEnv();

    auto *argValues = new jvalue[argumentCount];
    if (argumentCount > 0)
    {
      _fillArgs(arguments, dataTypes, argValues, argumentCount, stringTypeBitmask);
    }

    char *methodSignature = generateSignature(dataTypes, argumentCount, returnType);
    DNDebug("call method %s %s", methodName, methodSignature);
    jmethodID method = env->GetMethodID(cls, methodName, methodSignature);

    auto it = methodCallerMap.find(*returnType);
    if (it == methodCallerMap.end())
    {
      if (strcmp(returnType, "Ljava/lang/String;") == 0)
      {
        nativeInvokeResult = callNativeStringMethod(env, object, method, argValues);
      }
      else
      {
        jobject obj = env->CallObjectMethodA(object, method, argValues);
        if (obj != nullptr)
        {
          if (env->IsInstanceOf(obj, gStrCls->Object()))
          {
            /// mark the last pointer as string
            /// dart will check this pointer
            dataTypes[argumentCount] = (char *)"java.lang.String";
            nativeInvokeResult = convertToDartUtf16(env, (jstring)obj);
          }
          else
          {
            jclass objCls = env->GetObjectClass(obj);
            jobject gObj = env->NewGlobalRef(obj);
            _addGlobalObject(gObj, static_cast<jclass>(env->NewGlobalRef(objCls)));
            nativeInvokeResult = gObj;

            env->DeleteLocalRef(objCls);
            env->DeleteLocalRef(obj);
          }
        }
      }
    }
    else
    {
      nativeInvokeResult = it->second(env, object, method, argValues);
    }

    delete[] argValues;
    free(methodSignature);
    return nativeInvokeResult;
  }

  /// register listener object by using java dynamic proxy
  void registerNativeCallback(void *dartObject, char *clsName, char *funName, void *callback, Dart_Port dartPort)
  {
    JNIEnv *env = _getEnv();
    jclass callbackManager = _findClass(env, "com/dartnative/dart_native/CallbackManager");
    jmethodID registerCallback = env->GetStaticMethodID(callbackManager,
                                                        "registerCallback",
                                                        "(JLjava/lang/String;)Ljava/lang/Object;");

    auto dartObjectAddress = (jlong)dartObject;
    /// create interface object using java dynamic proxy
    jobject proxyObject = env->CallStaticObjectMethod(callbackManager,
                                                      registerCallback,
                                                      dartObjectAddress, env->NewStringUTF(clsName));
    jobject gProxyObj = env->NewGlobalRef(proxyObject);

    /// save object into cache
    doRegisterNativeCallback(dartObject, gProxyObj, funName, callback, dartPort);
    env->DeleteLocalRef(callbackManager);
    env->DeleteLocalRef(proxyObject);
  }

  /// init dart extensions
  intptr_t InitDartApiDL(void *data)
  {
    return Dart_InitializeApiDL(data);
  }

  /// reference counter
  void _updateObjectReference(jobject globalObject, bool isRetain)
  {
    std::lock_guard<std::mutex> lockGuard(globalReferenceMtx);
    DNDebug("_updateObjectReference %s", isRetain ? "retain" : "release");
    auto it = objectGlobalReference.find(globalObject);
    if (it == objectGlobalReference.end())
    {
      DNError("_updateObjectReference %s error not contain this object!!!", isRetain ? "retain" : "release");
      return;
    }

    if (isRetain)
    {
      /// dart object retain this dart object
      /// reference++
      it->second.second += 1;
      return;
    }

    /// release reference--
    it->second.second -= 1;

    /// no dart object retained this native object
    if (it->second.second <= 0)
    {
      JNIEnv *env = _getEnv();
      env->DeleteGlobalRef(it->second.first);
      objectGlobalReference.erase(it);
      env->DeleteGlobalRef(globalObject);
    }
  }

  /// release native object from cache
  static void RunFinalizer(void *isolate_callback_data,
                           Dart_WeakPersistentHandle handle,
                           void *peer)
  {
    _updateObjectReference(static_cast<jobject>(peer), false);
  }

  /// retain native object
  void PassObjectToCUseDynamicLinking(Dart_Handle h, void *objPtr)
  {
    if (Dart_IsError_DL(h))
    {
      DNError("Dart_IsError_DL");
      return;
    }
    _updateObjectReference(static_cast<jobject>(objPtr), true);
    intptr_t size = 8;
    Dart_NewWeakPersistentHandle_DL(h, objPtr, size, RunFinalizer);
  }

  /// dart notify run callback function
  void ExecuteCallback(Work *work_ptr)
  {
    const Work work = *work_ptr;
    work();
    delete work_ptr;
  }

  /// notify dart run callback function
  bool NotifyDart(Dart_Port send_port, const Work *work)
  {
    const auto work_addr = reinterpret_cast<intptr_t>(work);

    Dart_CObject dart_object;
    dart_object.type = Dart_CObject_kInt64;
    dart_object.value.as_int64 = work_addr;

    const bool result = Dart_PostCObject_DL(send_port, &dart_object);
    if (!result)
    {
      DNDebug("Native callback to Dart failed! Invalid port or isolate died");
    }
    return result;
  }

  JNIEXPORT jobject JNICALL Java_com_dartnative_dart_1native_CallbackInvocationHandler_hookCallback(JNIEnv *env,
                                                                                                    jclass clazz,
                                                                                                    jlong dartObjectAddress,
                                                                                                    jstring functionName,
                                                                                                    jint argumentCount,
                                                                                                    jobjectArray argumentTypes,
                                                                                                    jobjectArray argumentsArray,
                                                                                                    jstring returnTypeStr)
  {
    Dart_Port port = getCallbackDartPort(dartObjectAddress);
    if (port == 0)
    {
      DNError("not register this dart port!");
      return nullptr;
    }

    char *funName = (char *)env->GetStringUTFChars(functionName, 0);
    char **dataTypes = new char *[argumentCount + 1];
    void **arguments = new void *[argumentCount + 1];

    /// store argument to pointer
    for (int i = 0; i < argumentCount; ++i)
    {
      auto argTypeString = (jstring)env->GetObjectArrayElement(argumentTypes, i);
      auto argument = env->GetObjectArrayElement(argumentsArray, i);
      dataTypes[i] = (char *)env->GetStringUTFChars(argTypeString, 0);
      if (strcmp(dataTypes[i], "java.lang.String") == 0)
      {
        arguments[i] = (jstring)argument == nullptr ? reinterpret_cast<uint16_t *>((char *)"")
                                                    : convertToDartUtf16(env, (jstring)argument);
      }
      else
      {
        jclass objCls = env->GetObjectClass(argument);
        jobject gObj = env->NewGlobalRef(argument);
        _addGlobalObject(gObj, static_cast<jclass>(env->NewGlobalRef(objCls)));
        arguments[i] = gObj;

        env->DeleteLocalRef(objCls);
      }

      env->DeleteLocalRef(argTypeString);
      env->DeleteLocalRef(argument);
    }

    /// when return void, jstring which from native is null.
    char *returnType = returnTypeStr == nullptr ? nullptr
                                                : (char *)env->GetStringUTFChars(returnTypeStr, 0);
    /// the last pointer is return type
    dataTypes[argumentCount] = returnType;

    sem_t sem;
    bool isSemInitSuccess = sem_init(&sem, 0, 0) == 0;

    const Work work = [dartObjectAddress, dataTypes, arguments, argumentCount, funName, &sem, isSemInitSuccess]() {
      NativeMethodCallback methodCallback = getCallbackMethod(dartObjectAddress, funName);
      void *target = (void *)dartObjectAddress;
      if (methodCallback != nullptr && target != nullptr)
      {
        methodCallback(target, funName, arguments, dataTypes, argumentCount);
      }
      if (isSemInitSuccess)
      {
        sem_post(&sem);
      }
    };

    const Work *work_ptr = new Work(work);
    /// check run result
    bool notifyResult = NotifyDart(port, work_ptr);
    jobject callbackResult = nullptr;
    if (isSemInitSuccess)
    {
      if (notifyResult)
      {
        sem_wait(&sem);
        if (returnType == nullptr || strcmp(returnType, "void") == 0)
        {
          DNDebug("Native callback to Dart return type is void");
        }
        else if (strcmp(returnType, "java.lang.String") == 0)
        {
          callbackResult = convertToJavaUtf16(env, (char *)arguments[argumentCount], nullptr, 0);
        }
        else
        {
          callbackResult = (jobject)arguments[argumentCount];
        }
      }
      sem_destroy(&sem);
    }

    env->ReleaseStringUTFChars(returnTypeStr, returnType);
    env->ReleaseStringUTFChars(functionName, funName);
    delete[] arguments;
    delete[] dataTypes;

    return callbackResult;
  }
}
