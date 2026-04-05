// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "BmiAppServiceJni"

#include <jni.h>

// Uses the same libbinder + FLAG_PRIVATE_VENDOR technique proven in
// BMICalculatorA / BMICalculatorB. This JNI library lives in BmiSystemService
// (system partition), not in the app. Apps never see binder internals.
//
// FLAG_PRIVATE_VENDOR explanation (repeating for this file's readers):
// bmid and calculatord are VENDOR-stability binder services. A system-partition
// proxy calling transact() without this flag hits stability enforcement in
// BpBinder::transact() and gets BAD_TYPE (-129). The flag lowers the required
// stability from LOCAL to VENDOR for this one call.
#include <binder/IServiceManager.h>
#include <binder/IBinder.h>
#include <binder/Parcel.h>
#include <log/log.h>

#include <string>

using android::defaultServiceManager;
using android::IBinder;
using android::Parcel;
using android::String16;
using android::sp;
using android::status_t;
using android::OK;

static const char* kBmiSvc  = "com.myoem.bmi.IBMIService/default";
static const char* kCalcSvc = "com.myoem.calculator.ICalculatorService/default";
static const char* kBmiDesc  = "com.myoem.bmi.IBMIService";
static const char* kCalcDesc = "com.myoem.calculator.ICalculatorService";

static const uint32_t TX_GET_BMI  = IBinder::FIRST_CALL_TRANSACTION;
static const uint32_t TX_ADD      = IBinder::FIRST_CALL_TRANSACTION;
static const uint32_t TX_SUBTRACT = IBinder::FIRST_CALL_TRANSACTION + 1;
static const uint32_t TX_MULTIPLY = IBinder::FIRST_CALL_TRANSACTION + 2;
static const uint32_t TX_DIVIDE   = IBinder::FIRST_CALL_TRANSACTION + 3;

static const int32_t EX_NONE             = 0;
static const int32_t EX_SERVICE_SPECIFIC = -8;

static void throwRE(JNIEnv* env, const char* msg) {
    jclass c = env->FindClass("java/lang/RuntimeException");
    if (c) env->ThrowNew(c, msg);
}

static sp<IBinder> getService(JNIEnv* env, const char* name) {
    sp<IBinder> b = defaultServiceManager()->checkService(String16(name));
    if (b == nullptr) throwRE(env, (std::string(name) + " not available").c_str());
    return b;
}

// ── JNI symbols are on BmiAppService class ────────────────────────────────────
// Package: com.myoem.bmiapp  Class: BmiAppService
// Prefix:  Java_com_myoem_bmiapp_BmiAppService_

// ── Availability ──────────────────────────────────────────────────────────────

extern "C" JNIEXPORT jboolean JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeIsBmiAvailable(JNIEnv*, jobject) {
    return defaultServiceManager()->checkService(String16(kBmiSvc)) != nullptr
           ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeIsCalcAvailable(JNIEnv*, jobject) {
    return defaultServiceManager()->checkService(String16(kCalcSvc)) != nullptr
           ? JNI_TRUE : JNI_FALSE;
}

// ── BMI ───────────────────────────────────────────────────────────────────────

extern "C" JNIEXPORT jfloat JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeGetBMI(
        JNIEnv* env, jobject, jfloat height, jfloat weight) {
    sp<IBinder> binder = getService(env, kBmiSvc);
    if (!binder) return 0.0f;

    Parcel data, reply;
    data.writeInterfaceToken(String16(kBmiDesc));
    data.writeFloat(static_cast<float>(height));
    data.writeFloat(static_cast<float>(weight));

    status_t ret = binder->transact(TX_GET_BMI, data, &reply,
                                    IBinder::FLAG_PRIVATE_VENDOR);
    if (ret != OK) {
        throwRE(env, ("getBMI transact failed: " + std::to_string(ret)).c_str());
        return 0.0f;
    }
    int32_t ex = reply.readInt32();
    if (ex == EX_NONE) {
        float result = reply.readFloat();
        ALOGD("getBMI(%.2f, %.2f) = %.2f", (float)height, (float)weight, result);
        return static_cast<jfloat>(result);
    }
    if (ex == EX_SERVICE_SPECIFIC) {
        int32_t code = reply.readInt32();
        throwRE(env, ("Invalid input (code " + std::to_string(code) + ")").c_str());
    } else {
        throwRE(env, ("getBMI: unexpected exception " + std::to_string(ex)).c_str());
    }
    return 0.0f;
}

// ── Calculator ────────────────────────────────────────────────────────────────

static jint calcOp(JNIEnv* env, uint32_t tx, const char* op, jint a, jint b) {
    sp<IBinder> binder = getService(env, kCalcSvc);
    if (!binder) return 0;

    Parcel data, reply;
    data.writeInterfaceToken(String16(kCalcDesc));
    data.writeInt32(static_cast<int32_t>(a));
    data.writeInt32(static_cast<int32_t>(b));

    status_t ret = binder->transact(tx, data, &reply, IBinder::FLAG_PRIVATE_VENDOR);
    if (ret != OK) {
        throwRE(env, (std::string(op) + " transact failed: " + std::to_string(ret)).c_str());
        return 0;
    }
    int32_t ex = reply.readInt32();
    if (ex == EX_NONE) return static_cast<jint>(reply.readInt32());
    if (ex == EX_SERVICE_SPECIFIC) throwRE(env, "Division by zero");
    else throwRE(env, (std::string(op) + ": unexpected exception " + std::to_string(ex)).c_str());
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeAdd(
        JNIEnv* e, jobject, jint a, jint b) { return calcOp(e, TX_ADD, "add", a, b); }

extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeSubtract(
        JNIEnv* e, jobject, jint a, jint b) { return calcOp(e, TX_SUBTRACT, "subtract", a, b); }

extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeMultiply(
        JNIEnv* e, jobject, jint a, jint b) { return calcOp(e, TX_MULTIPLY, "multiply", a, b); }

extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmiapp_BmiAppService_nativeDivide(
        JNIEnv* e, jobject, jint a, jint b) { return calcOp(e, TX_DIVIDE, "divide", a, b); }
