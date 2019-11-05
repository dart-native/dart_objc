import 'dart:ffi';

import 'package:dart_objc/runtime.dart';
import 'package:dart_objc/src/common/channel_dispatch.dart';
import 'package:dart_objc/src/common/pointer_encoding.dart';
import 'package:dart_objc/src/runtime/id.dart';
import 'package:dart_objc/src/runtime/native_runtime.dart';
import 'package:dart_objc/src/runtime/selector.dart';
import 'package:ffi/ffi.dart';

Map<Pointer<Void>, Map<Pointer<Void>, Function>> _cache = {};

bool registerDelegate(
    id target, Selector selector, Function function, Protocol protocol) {
  Map<Pointer<Void>, Function> methodsMap = _cache[target.pointer];
  if (methodsMap == null) {
    methodsMap = {selector.toPointer(): function};
  } else {
    methodsMap[selector.toPointer()] = function;
  }
  _cache[target.pointer] = methodsMap;
  int result = nativeAddMethod(
      target.pointer, selector.toPointer(), protocol.toPointer(), _callbackPtr);
  ChannelDispatch().registerChannelCallback('method_delegate', _asyncCallback);
  return result != 0;
}

removeDelegate(id target) {
  _cache.remove(target.pointer);
}

Pointer<NativeFunction<MethodIMPCallbackC>> _callbackPtr =
    Pointer.fromFunction(_syncCallback);

_callback(
    Pointer<Void> targetPtr,
    Pointer<Void> selPtr,
    Pointer<Pointer<Pointer<Void>>> argsPtrPtr,
    Pointer<Pointer<Void>> retPtr,
    int argCount,
    Pointer<Pointer<Utf8>> typesPtrPtr) {
  Map<Pointer<Void>, Function> methodsMap = _cache[targetPtr];
  if (methodsMap == null) {
    return null;
  }
  List args = [];

  for (var i = 0; i < argCount; i++) {
    String encoding = Utf8.fromUtf8(typesPtrPtr.elementAt(i + 2).load());
    Pointer ptr = argsPtrPtr.elementAt(i).load();
    if (!encoding.startsWith('{')) {
      ptr = ptr.cast<Pointer<Void>>().load();
    }
    dynamic value = loadValueFromPointer(ptr, encoding);
    args.add(value);
  }

  Function function = methodsMap[selPtr];
  dynamic result = Function.apply(function, args);

  if (retPtr != null) {
    String encoding = Utf8.fromUtf8(typesPtrPtr.elementAt(0).load());
    Function closure = storeValueToPointer(result, retPtr, encoding);
    if (closure != null) {
      throw 'Return value of callback may leak.';
    }
  }
  return result;
}

_syncCallback(
    Pointer<Void> targetPtr,
    Pointer<Void> selPtr,
    Pointer<Pointer<Pointer<Void>>> argsPtrPtr,
    Pointer<Pointer<Void>> retPtr,
    int argCount,
    Pointer<Pointer<Utf8>> typesPtrPtr) {
  _callback(targetPtr, selPtr, argsPtrPtr, retPtr, argCount, typesPtrPtr);
}

dynamic _asyncCallback(
    int targetAddr, int selAddr, int argsAddr, int argCount, int typesAddr) {
  Pointer<Void> targetPtr = Pointer.fromAddress(targetAddr);
  Pointer<Void> selPtr = Pointer.fromAddress(selAddr);
  Pointer<Pointer<Pointer<Void>>> argsPtrPtr = Pointer.fromAddress(argsAddr);
  Pointer<Pointer<Utf8>> typesPtrPtr = Pointer.fromAddress(typesAddr);
  return _callback(targetPtr, selPtr, argsPtrPtr, null, argCount, typesPtrPtr);
}