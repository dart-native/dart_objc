//
// Created by Hui on 6/9/21.
//

#ifndef DART_NATIVE_JNI_OBJECT_REF_H
#define DART_NATIVE_JNI_OBJECT_REF_H

#include <jni.h>

/**
 * Global reference will auto delete in destructor
 */
template <typename T>
class JavaGlobalRef {
 public:
  explicit JavaGlobalRef(T t, JNIEnv *env): env(env) {
    this->obj = env->NewGlobalRef(t);
  }

  JavaGlobalRef() = delete;

  T Object() const { return static_cast<T>(obj); }

  ~JavaGlobalRef() { this->DeleteGlobalRef(); }

 protected:
  jobject obj = nullptr;

 private:
  JNIEnv *env;

  void DeleteGlobalRef() {
    if (this->obj) {
      env->DeleteGlobalRef(this->obj);
      this->obj = nullptr;
    }
  }
};

#endif //DART_NATIVE_JNI_OBJECT_REF_H
