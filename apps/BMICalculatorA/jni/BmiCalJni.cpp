// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "BmiCalJni"

#include <jni.h>

// Use libbinder (not libbinder_ndk) with FLAG_PRIVATE_VENDOR to bypass the
// cross-partition stability check in BpBinder::transact.
//
// Root cause: bmid and calculatord are NDK AIDL vendor services → VENDOR
// stability.  A system-partition JNI library has LOCAL stability.
// BpBinder::transact rejects LOCAL→VENDOR calls with BAD_TYPE.
//
// FLAG_PRIVATE_VENDOR makes BpBinder require only VENDOR stability (not LOCAL),
// so VENDOR→VENDOR passes.  The actual binder transaction is identical;
// only the client-side stability gate is relaxed.
//
// Parcel format written here matches what the NDK AIDL server's
// AParcel_enforceInterface + AParcel_readFloat/Int32 expect:
//   request:  [writeInterfaceToken (strictMode+workSrc+descriptor)][params…]
//   reply:    [int32 exceptionCode][result or errorCode]
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

// ── Service lookup names (must match main.cpp kServiceName) ──────────────────
static const char* kBmiSvc  = "com.myoem.bmi.IBMIService/default";
static const char* kCalcSvc = "com.myoem.calculator.ICalculatorService/default";

// ── Interface descriptors (must match AIDL package + interface name) ─────────
static const char* kBmiDesc  = "com.myoem.bmi.IBMIService";
static const char* kCalcDesc = "com.myoem.calculator.ICalculatorService";

// ── Transaction codes ─────────────────────────────────────────────────────────
static const uint32_t TX_GET_BMI   = IBinder::FIRST_CALL_TRANSACTION;
static const uint32_t TX_ADD       = IBinder::FIRST_CALL_TRANSACTION;
static const uint32_t TX_SUBTRACT  = IBinder::FIRST_CALL_TRANSACTION + 1;
static const uint32_t TX_MULTIPLY  = IBinder::FIRST_CALL_TRANSACTION + 2;
static const uint32_t TX_DIVIDE    = IBinder::FIRST_CALL_TRANSACTION + 3;

// ── NDK AIDL reply exception codes ───────────────────────────────────────────
static const int32_t EX_NONE             = 0;
static const int32_t EX_SERVICE_SPECIFIC = -8;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void throwRE(JNIEnv* env, const char* msg) {
    jclass c = env->FindClass("java/lang/RuntimeException");
    if (c) env->ThrowNew(c, msg);
}

static sp<IBinder> getService(JNIEnv* env, const char* name) {
    sp<IBinder> b = defaultServiceManager()->checkService(String16(name));
    if (b == nullptr) throwRE(env, (std::string(name) + " not available").c_str());
    return b;
}

// ── Service availability probes ───────────────────────────────────────────────

extern "C" JNIEXPORT jboolean JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_isBmiServiceAvailable(JNIEnv*, jobject) {
    return defaultServiceManager()->checkService(String16(kBmiSvc)) != nullptr
           ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_isCalcServiceAvailable(JNIEnv*, jobject) {
    return defaultServiceManager()->checkService(String16(kCalcSvc)) != nullptr
           ? JNI_TRUE : JNI_FALSE;
}

// ── BMI ───────────────────────────────────────────────────────────────────────

extern "C" JNIEXPORT jfloat JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_getBMI(
        JNIEnv* env, jobject, jfloat height, jfloat weight) {
    sp<IBinder> binder = getService(env, kBmiSvc);
    if (!binder) return 0.0f;

    Parcel data, reply;
    data.writeInterfaceToken(String16(kBmiDesc));
    data.writeFloat(static_cast<float>(height));
    data.writeFloat(static_cast<float>(weight));

    // FLAG_PRIVATE_VENDOR: lowers required stability to VENDOR in BpBinder::transact,
    // allowing a system-partition caller to reach a VENDOR-stability binder.
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
    } else if (ex == EX_SERVICE_SPECIFIC) {
        int32_t code = reply.readInt32();
        throwRE(env, ("Invalid input (code " + std::to_string(code) + ")").c_str());
    } else {
        throwRE(env, ("getBMI: unexpected exception " + std::to_string(ex)).c_str());
    }
    return 0.0f;
}

// ── Calculator helper ─────────────────────────────────────────────────────────

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
Java_com_myoem_bmicalculatora_NativeBinder_calcAdd(JNIEnv* e, jobject, jint a, jint b) {
    return calcOp(e, TX_ADD, "add", a, b);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_calcSubtract(JNIEnv* e, jobject, jint a, jint b) {
    return calcOp(e, TX_SUBTRACT, "subtract", a, b);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_calcMultiply(JNIEnv* e, jobject, jint a, jint b) {
    return calcOp(e, TX_MULTIPLY, "multiply", a, b);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_calcDivide(JNIEnv* e, jobject, jint a, jint b) {
    return calcOp(e, TX_DIVIDE, "divide", a, b);
}
