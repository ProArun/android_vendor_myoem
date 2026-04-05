# Android Binder — Complete Guide: Basic to Advanced

> **Author's Note:** This guide is written from the perspective of an experienced Android AOSP engineer teaching Binder from the ground up. Every concept builds on the previous one. By the end, you will understand not just how to *use* Binder, but *why* it works the way it does — from the kernel driver all the way up to your AIDL-generated Java interface.

---

## Learning Path Index

| Module | Topic | Level |
|--------|-------|-------|
| [Module 1](#module-1-why-binder-exists) | Why Binder Exists | Foundation |
| [Module 2](#module-2-binder-architecture-kernel--userspace) | Binder Architecture (Kernel ↔ Userspace) | Core |
| [Module 3](#module-3-userspace-library--libbinder-internals) | Userspace Library — libbinder internals | Core |
| [Module 4](#module-4-aidl--the-code-generator) | AIDL — The Code Generator | Practical |
| [Module 5](#module-5-service-manager-deep-dive) | Service Manager Deep Dive | Practical |
| [Module 6](#module-6-transaction-lifecycle-end-to-end) | Transaction Lifecycle (End-to-End) | Deep Dive |
| [Module 7](#module-7-binder-in-java-framework-side) | Binder in Java (Framework Side) | Framework |
| [Module 8](#module-8-stability--versioning-aosp-10) | Stability & Versioning (AOSP 10+) | Advanced |
| [Module 9](#module-9-death-recipients--error-handling) | Death Recipients & Error Handling | Advanced |
| [Module 10](#module-10-selinux--binder-security) | SELinux & Binder Security | Advanced |
| [Module 11](#module-11-advanced-topics) | Advanced Internals | Expert |

---

---

## Module 1: Why Binder Exists

### The Problem: IPC on Linux

Android is a multi-process operating system. Every app, every system service, every HAL runs in its own process. This is by design — process isolation means one crashing app cannot corrupt another app's memory.

But processes need to talk to each other. A camera app needs to ask the camera service to open the camera. A music player needs to talk to the audio service. This is **Inter-Process Communication (IPC)**.

Linux already provides several IPC mechanisms. Let's look at each and understand why Android couldn't use them directly.

---

### Linux IPC Options and Their Limitations

#### 1. Pipes
```
Process A  ──write──►  pipe buffer  ──read──►  Process B
```
- Simple, unidirectional byte stream.
- **Problem:** No identity. Process B has no way to verify who Process A is. Any process can write to a pipe. For a system service managing sensitive hardware, this is a security disaster.
- **Problem:** Two copies of data — one kernel→pipe-buffer, one pipe-buffer→Process B.

#### 2. Unix Domain Sockets
```
Process A  ──send──►  kernel socket buffer  ──recv──►  Process B
```
- Bidirectional, can pass file descriptors.
- **Problem:** Still two copies of data in most cases.
- **Problem:** Identity is weak — you can get the peer's PID with `SO_PEERCRED` but it's clunky and not enforced at every call.
- **Problem:** No structured RPC model — you're sending raw bytes, you must design your own protocol.

#### 3. Shared Memory (SysV / POSIX)
```
Process A  ──writes──►  [shared page]  ◄──reads──  Process B
```
- Fastest possible — zero copies after setup.
- **Problem:** Requires explicit synchronization (mutexes, semaphores). Very hard to get right.
- **Problem:** No identity at all. Anyone who can get the shared memory handle can read/write it.
- **Problem:** Memory management nightmare — who owns the pages? When do you free them?

#### 4. D-Bus (used on desktop Linux)
- Structured RPC over Unix sockets, widely used on desktop Linux.
- **Problem:** Too slow for mobile. D-Bus requires a central broker daemon, adding latency.
- **Problem:** Not designed for the permission/identity model Android needs.

---

### What Android Actually Needs

Android has very specific requirements that none of the above satisfy cleanly:

1. **Caller Identity** — Every IPC call must carry a verified caller UID and PID. A banking app asking the keystore service for a key — the keystore must *know* it's talking to that specific app, not a malicious app impersonating it. The kernel must enforce this, not the app.

2. **Performance** — Android runs on battery-powered devices. IPC happens thousands of times per second (UI rendering alone involves dozens of Binder calls per frame). Every extra data copy wastes CPU cycles and memory bandwidth.

3. **Object-Oriented RPC** — Developers need to call remote methods like local ones. Raw byte streams are too low-level. There needs to be a framework that handles marshalling, routing, and errors.

4. **Synchronous call-return semantics** — When your app calls `getSystemService()` and then calls a method on that service, it should block and get back a result, just like a normal function call.

5. **Reference counting across processes** — If Process A holds a reference to a Binder object in Process B, Process B should not exit while A still holds that reference.

---

### The Binder Solution

Binder was originally developed by Be Inc. for BeOS, then brought to Android by Dianne Hackborn at Google. It solves all five requirements:

**Identity:** The kernel itself tags every Binder transaction with the sender's UID and PID. The server-side library reads these from the kernel — there is no way for a client to forge them. This is the foundation of Android's security model.

**Performance:** Binder uses a clever `mmap` trick. The kernel maps the same physical memory pages into both the sender's virtual address space and the receiver's virtual address space. Data is written once by the sender, and the receiver reads it directly — **one copy**, not two.

```
Sender process VA:   [data]
                       |
                  kernel maps
                       |
Receiver process VA: [data]  ← same physical page, no second copy
```

**Object-Oriented RPC:** The AIDL tool generates typed proxy and stub classes. You define an interface, AIDL generates the marshalling code, and you call remote methods as if they were local.

**Synchronous semantics:** When a client thread makes a Binder call, the kernel puts that thread to sleep and wakes a thread in the server process. When the server replies, the kernel wakes the client thread with the result. From the client's perspective, it called a function and got a return value.

**Reference counting:** The kernel tracks how many handles point to each Binder object. When the last handle is released, the kernel notifies the owning process.

---

### The `/dev/binder` Device

The entire Binder IPC mechanism lives in a Linux kernel driver: `drivers/android/binder.c`. This driver exposes a character device at `/dev/binder`. Every process that wants to use Binder opens this device and uses `ioctl` to communicate with the kernel.

On Android there are actually **three** separate Binder domains:
- `/dev/binder` — framework/app Binder (the one most people mean)
- `/dev/hwbinder` — hardware HAL Binder (HIDL)
- `/dev/vndbinder` — vendor service Binder

This separation prevents vendor code from directly accessing framework Binder objects and vice versa (important for Project Treble / VNDK).

---

### Summary of Module 1

| Problem | Binder's Solution |
|---------|-----------------|
| No caller identity | Kernel tags every transaction with sender UID/PID |
| Two data copies | mmap trick — one copy |
| Raw byte streams | AIDL generates typed RPC code |
| No synchronous semantics | Kernel manages thread sleep/wake across processes |
| No cross-process refcounting | Kernel tracks handle reference counts |

---

### Module 1 — Self-Check Questions

1. Why can't a client process fake its UID in a Binder call?
2. What is the "one copy" trick in Binder, and how does `mmap` enable it?
3. Why does Android have three separate Binder devices (`/dev/binder`, `/dev/hwbinder`, `/dev/vndbinder`)? What problem does this solve?
4. D-Bus is used on desktop Linux for IPC. Why wasn't it chosen for Android?
5. In your ThermalControl project, `thermalcontrold` is a server and `thermalcontrol_client` is a client. Which process opens `/dev/binder`? Both or just one?

---

---

## Module 2: Binder Architecture (Kernel ↔ Userspace)

### Overview

Before we look at C++ classes and AIDL, we need to understand what's happening at the kernel level. The kernel is the single source of truth for Binder — everything in libbinder is just a userspace wrapper around kernel `ioctl` calls.

```
┌─────────────────────────────────────────────────────────┐
│                   Client Process                         │
│  BpBinder → IPCThreadState → ioctl(BINDER_WRITE_READ)  │
└─────────────────────────────┬───────────────────────────┘
                              │  /dev/binder
┌─────────────────────────────▼───────────────────────────┐
│                  Linux Kernel                            │
│              drivers/android/binder.c                    │
│   binder_transaction() → find server → copy data        │
└─────────────────────────────┬───────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────┐
│                   Server Process                         │
│  IPCThreadState ← ioctl(BINDER_WRITE_READ) ← BBinder   │
└─────────────────────────────────────────────────────────┘
```

---

### The ioctl Interface

Every Binder operation from userspace goes through one primary `ioctl` command:

```c
ioctl(fd, BINDER_WRITE_READ, &bwr);
```

Where `bwr` is a `binder_write_read` struct:

```c
struct binder_write_read {
    binder_size_t  write_size;      // bytes to write to kernel
    binder_size_t  write_consumed;  // bytes kernel consumed
    binder_uintptr_t write_buffer;  // pointer to write data
    binder_size_t  read_size;       // bytes to read from kernel
    binder_size_t  read_consumed;   // bytes kernel put in buffer
    binder_uintptr_t read_buffer;   // pointer to read buffer
};
```

You can both write commands to the kernel AND read commands from the kernel in a single `ioctl`. This is a key performance optimization — one syscall does both directions.

---

### BC_ Commands (Browser→Context, i.e., Userspace→Kernel)

These are commands your process sends *to* the kernel:

| Command | Meaning |
|---------|---------|
| `BC_TRANSACTION` | Send a Binder transaction to another process |
| `BC_REPLY` | Send a reply to a transaction you received |
| `BC_FREE_BUFFER` | Tell kernel you're done reading a received buffer |
| `BC_INCREFS` / `BC_ACQUIRE` | Increment reference count on a Binder object |
| `BC_DECREFS` / `BC_RELEASE` | Decrement reference count |
| `BC_REGISTER_LOOPER` | Tell kernel this thread is joining the thread pool |
| `BC_ENTER_LOOPER` | Tell kernel this is the main looper thread |
| `BC_EXIT_LOOPER` | Leave the looper |
| `BC_REQUEST_DEATH_NOTIFICATION` | Ask to be notified when a Binder dies |

---

### BR_ Commands (Binder→Receiver, i.e., Kernel→Userspace)

These are commands the kernel sends *to* your process (you read them):

| Command | Meaning |
|---------|---------|
| `BR_TRANSACTION` | You have an incoming transaction to handle |
| `BR_REPLY` | Here is the reply to a transaction you sent |
| `BR_TRANSACTION_COMPLETE` | Your `BC_TRANSACTION` was delivered (one-way) |
| `BR_DEAD_BINDER` | A Binder object you were watching has died |
| `BR_INCREFS` / `BR_ACQUIRE` | Kernel asks you to increment your refcount |
| `BR_DECREFS` / `BR_RELEASE` | Kernel asks you to decrement your refcount |
| `BR_SPAWN_LOOPER` | Kernel tells you to spawn another thread |
| `BR_ERROR` | An error occurred |
| `BR_OK` | Acknowledgment |

---

### binder_transaction_data — The Heart of a Binder Call

Every transaction carries a `binder_transaction_data` struct:

```c
struct binder_transaction_data {
    union {
        __u32 handle;       // client side: handle to target
        binder_uintptr_t ptr; // server side: pointer to BBinder
    } target;
    binder_uintptr_t    cookie;     // server: BBinder*
    __u32               code;       // which method to call (transaction code)
    __u32               flags;      // TF_ONE_WAY, TF_ROOT_OBJECT, etc.
    pid_t               sender_pid;
    uid_t               sender_euid;
    binder_size_t       data_size;  // size of data buffer
    binder_size_t       offsets_size; // size of offsets array
    union {
        struct {
            binder_uintptr_t buffer;  // pointer to data
            binder_uintptr_t offsets; // offsets of embedded objects
        } ptr;
        __u8 buf[8];
    } data;
};
```

Key fields to understand:
- **`target.handle`** — On the client side, this is a numeric handle (like a file descriptor number) that identifies which Binder object you're calling. Handle `0` always means the Service Manager.
- **`code`** — This is the method number. When AIDL generates `onTransact()`, each method gets a unique integer code (like `TRANSACTION_getTemperature = 1`).
- **`sender_euid`** — The kernel fills this in. The client cannot set this. This is how identity works.
- **`flags`** — If `TF_ONE_WAY` is set, this is a fire-and-forget call (no reply expected).

---

### Binder Objects in the Kernel

The kernel maintains several data structures per process:

```
struct binder_proc {          // one per process
    struct binder_thread *threads;  // thread pool
    struct binder_node *nodes;      // Binder objects THIS process owns
    struct binder_ref *refs;        // handles to remote Binder objects
    struct binder_buffer *buffers;  // mmap'd memory regions
};

struct binder_node {          // a Binder object (server side)
    void *cookie;             // BBinder* pointer in userspace
    struct binder_ref *refs;  // all clients holding a handle to this
};

struct binder_ref {           // a handle (client side)
    uint32_t desc;            // the handle number used in BC_TRANSACTION
    struct binder_node *node; // the actual binder_node this refers to
};
```

When a client does `BC_TRANSACTION` with handle `5`, the kernel looks up handle `5` in that client's `binder_ref` table, finds the corresponding `binder_node`, and delivers the transaction to the process that owns that node.

---

### The mmap Region

When a process first opens `/dev/binder` and calls `mmap`, the kernel allocates a memory region (default 1MB, up to 4MB) that is mapped into that process's virtual address space. This is the **receive buffer**.

When another process sends data to you via `BC_TRANSACTION`, the kernel:
1. Finds your process's mmap'd region.
2. Copies the data directly into that region.
3. Tells you the address via `BR_TRANSACTION`.

The sender wrote the data once (into the kernel). The receiver reads it from the mapped region directly. No second copy. This is the efficiency win.

---

### Thread Pool Management

The kernel actively manages threads. When there are too many pending transactions and not enough threads to handle them, the kernel sends `BR_SPAWN_LOOPER` to tell the process to create a new thread. This is why `ProcessState::startThreadPool()` sets a maximum thread count — you don't want unbounded thread creation.

```
kernel sees: 15 pending transactions, only 2 threads
kernel sends: BR_SPAWN_LOOPER
ProcessState sees BR_SPAWN_LOOPER → creates new thread → thread calls joinThreadPool()
```

---

### Module 2 — Self-Check Questions

1. What is the difference between `BC_TRANSACTION` and `BR_TRANSACTION`? Who sends each?
2. If a client sends a Binder call with `TF_ONE_WAY` flag, does the client thread block waiting for a reply?
3. How does the kernel know which process to deliver a `BC_TRANSACTION` to? What data structure does it use?
4. Why is handle `0` special? What does every process get for free by knowing handle `0`?
5. In `binder_transaction_data`, who fills in `sender_euid` — the client or the kernel? Why does this matter for security?
6. What happens if a server process is slow and the kernel sends `BR_SPAWN_LOOPER` 100 times? What protects against thread explosion?

---

---

## Module 3: Userspace Library — libbinder Internals

### Overview

Now that you understand the kernel layer, let's look at the C++ classes in `libbinder` that wrap those `ioctl` calls into something usable. These classes live in `frameworks/native/libs/binder/`.

The class hierarchy looks like this:

```
IBinder (abstract interface)
├── BBinder      (server side — you are the Binder object)
└── BpBinder     (client side — a proxy to a remote Binder object)

ProcessState     (one per process — manages the /dev/binder fd)
IPCThreadState   (one per thread — drives the ioctl loop)
Parcel           (serialization container)
IServiceManager  (interface to ServiceManager, handle 0)
```

---

### ProcessState — The Process-Level Singleton

`ProcessState` opens `/dev/binder` and calls `mmap`. There is exactly one instance per process (it's a singleton). It is created the first time you call `ProcessState::self()`.

```cpp
// frameworks/native/libs/binder/ProcessState.cpp (simplified)
sp<ProcessState> ProcessState::self() {
    static sp<ProcessState> gProcess;
    if (gProcess == nullptr) {
        gProcess = new ProcessState("/dev/binder");
    }
    return gProcess;
}

ProcessState::ProcessState(const char* driver) {
    mDriverFD = open(driver, O_RDWR | O_CLOEXEC);
    mmap(nullptr, BINDER_VM_SIZE, PROT_READ, MAP_PRIVATE | MAP_NORESERVE,
         mDriverFD, 0);
    // BINDER_VM_SIZE = 1MB for apps, configurable
}
```

Key responsibilities:
- Opens `/dev/binder` (once, on first use).
- Calls `mmap` to set up the receive buffer.
- Creates and tracks threads in the Binder thread pool.
- `startThreadPool()` — spawns the first looper thread and listens for `BR_SPAWN_LOOPER` to spawn more.
- `setThreadPoolMaxThreadCount(n)` — limits how many threads can be spawned.

**Important:** `ProcessState::self()` does NOT start any threads. It just opens the driver. You must call `startThreadPool()` to actually start receiving transactions.

---

### IPCThreadState — The Per-Thread Workhorse

While `ProcessState` is shared across the process, `IPCThreadState` is created per-thread (stored in thread-local storage). It is the one that actually reads and writes the `ioctl`.

```cpp
// Simplified view of the transaction send path
status_t IPCThreadState::transact(int32_t handle,
                                   uint32_t code,
                                   const Parcel& data,
                                   Parcel* reply,
                                   uint32_t flags) {
    writeTransactionData(BC_TRANSACTION, flags, handle, code, data);
    
    if (flags & TF_ONE_WAY) {
        return flushCommands();        // fire and forget
    } else {
        return waitForResponse(reply); // block until reply
    }
}

status_t IPCThreadState::waitForResponse(Parcel* reply) {
    while (true) {
        talkWithDriver();              // ioctl(BINDER_WRITE_READ, ...)
        // reads BR_ commands from kernel
        switch (cmd) {
            case BR_REPLY: // got our reply
                reply->setData(tr.data); return NO_ERROR;
            case BR_TRANSACTION: // someone is calling US while we wait
                executeCommand(cmd);  // handle it, then keep waiting
            case BR_DEAD_REPLY:
                return DEAD_OBJECT;
        }
    }
}
```

Notice something subtle: while waiting for a reply, the thread can receive `BR_TRANSACTION` and handle it. This is how Binder handles **re-entrant calls** — if service A calls service B, and service B calls back into service A, it works without deadlock because A's waiting thread handles the incoming call.

The `joinThreadPool()` function is the loop that makes a thread a permanent Binder server thread:

```cpp
void IPCThreadState::joinThreadPool(bool isMain) {
    mOut.writeInt32(isMain ? BC_ENTER_LOOPER : BC_REGISTER_LOOPER);
    
    while (true) {
        talkWithDriver();
        switch (cmd) {
            case BR_TRANSACTION: executeCommand(BR_TRANSACTION); break;
            case BR_DEAD_BINDER: executeCommand(BR_DEAD_BINDER); break;
            case BR_SPAWN_LOOPER: mProcess->spawnPooledThread(false); break;
            case BR_EXIT_LOOPER: return;  // we're done
        }
    }
}
```

---

### IBinder, BBinder, BpBinder

These three form the object model for Binder references.

#### IBinder — The Abstract Interface

```cpp
class IBinder : public virtual RefBase {
public:
    virtual status_t transact(uint32_t code, const Parcel& data,
                               Parcel* reply, uint32_t flags = 0) = 0;
    virtual bool pingBinder() = 0;
    virtual status_t linkToDeath(const sp<DeathRecipient>& recipient) = 0;
};
```

`IBinder` is what you hold when you have *any* Binder reference — you don't need to know if it's local or remote.

#### BBinder — The Server-Side Object (B = Binder, the real thing)

```cpp
class BBinder : public IBinder {
public:
    virtual status_t onTransact(uint32_t code, const Parcel& data,
                                 Parcel* reply, uint32_t flags = 0);
    // onTransact() is what you override in your service
};
```

`BBinder` lives in the server process. When `IPCThreadState` receives `BR_TRANSACTION`, it calls `BBinder::transact()` which calls your `onTransact()` override. The AIDL-generated `BnXxx` class extends `BBinder`.

#### BpBinder — The Client-Side Proxy (Bp = Binder proxy)

```cpp
class BpBinder : public IBinder {
public:
    BpBinder(int32_t handle);  // handle is the kernel handle number
    
    status_t transact(uint32_t code, const Parcel& data,
                       Parcel* reply, uint32_t flags = 0) override {
        // delegates to IPCThreadState::transact()
        return IPCThreadState::self()->transact(mHandle, code, data,
                                                 reply, flags);
    }
private:
    int32_t mHandle; // the kernel handle number for the remote BBinder
};
```

`BpBinder` is what the client holds. When you call `defaultServiceManager()->getService("myservice")`, you get back a `BpBinder` wrapping the handle the kernel gave you for that service. The AIDL-generated `BpXxx` class wraps a `BpBinder`.

---

### Parcel — The Serialization Container

`Parcel` is the buffer you write data into before a Binder call, and read data from after a Binder call. It's not just a byte buffer — it understands Binder objects.

```cpp
Parcel data, reply;

// Writing
data.writeInt32(42);
data.writeString16(String16("hello"));
data.writeFloat(3.14f);
data.writeStrongBinder(someIBinder);  // embeds a Binder object reference!

// Sending
remote()->transact(TRANSACTION_myMethod, data, &reply);

// Reading the reply
int32_t result = reply.readInt32();
```

When you `writeStrongBinder()`, Parcel does something special — it records the offset of that Binder object in the data buffer. When the kernel copies the data, it sees those offsets and knows to translate the Binder object reference (from a pointer in the sender's process to a handle in the receiver's process). This is how Binder objects can be passed through transactions.

**Parcel memory layout:**
```
[data bytes...]  [offsets array]
  ^                ^
  raw data         positions of embedded Binder objects
```

---

### sp<> and wp<> — Smart Pointers and RefBase

Binder objects use Android's reference counting system, not `std::shared_ptr`. The base class is `RefBase`, and the smart pointers are `sp<>` (strong pointer) and `wp<>` (weak pointer).

```cpp
sp<IBinder> binder = getService();  // strong ref — object kept alive
wp<IBinder> weak = binder;          // weak ref — doesn't keep alive

sp<IBinder> promoted = weak.promote(); // null if object is dead
```

This matters for Binder because the kernel tracks how many strong references exist across processes. When the last strong reference is released (even from another process), the kernel notifies the owning process via `BR_RELEASE`.

---

### The Call Stack for a Binder Transaction (Server Side)

When your server receives a call, the call stack looks like this:

```
IPCThreadState::joinThreadPool()
  └── IPCThreadState::talkWithDriver()       [ioctl returns with BR_TRANSACTION]
        └── IPCThreadState::executeCommand()
              └── BBinder::transact()
                    └── YourService::onTransact()   [AIDL-generated BnXxx]
                          └── YourService::yourMethod()  [your actual code]
```

After `yourMethod()` returns, the reply `Parcel` is written back up the call stack, and `IPCThreadState` sends `BC_REPLY` to the kernel.

---

### Module 3 — Self-Check Questions

1. What is the difference between `ProcessState` and `IPCThreadState`? How many of each exist per process?
2. If you forget to call `ProcessState::self()->startThreadPool()` in your server's `main()`, what happens when a client tries to call your service?
3. `BpBinder` has a `mHandle` integer. What does this handle represent, and where does it come from?
4. Why can a Binder call be re-entrant without deadlocking? Trace through what happens when service A calls service B, and B calls back into A.
5. What is special about `writeStrongBinder()` compared to `writeInt32()`? What does the kernel do with the offsets array?
6. In your ThermalControl project, `ThermalControlService` extends what AIDL-generated class? Is that class a `BBinder` or `BpBinder`?

---

---

## Module 4: AIDL — The Code Generator

### What is AIDL?

AIDL (Android Interface Definition Language) is a code generator. You write a `.aidl` file describing your interface, and the build system generates:
- A **stub** (`BnXxx`) — the server-side class you extend. Handles `onTransact()`.
- A **proxy** (`BpXxx`) — the client-side class that marshals arguments and calls `transact()`.
- An **interface** (`IXxx`) — the abstract base both inherit from.

You write ~10 lines of AIDL and get ~300 lines of generated boilerplate for free. Let's understand what that boilerplate actually does.

---

### A Simple AIDL Interface

```aidl
// IThermalControlService.aidl
package com.myoem.thermalcontrol;

interface IThermalControlService {
    float getTemperature();
    void setFanSpeed(int speed);
    void registerCallback(IThermalCallback callback);
}
```

---

### What AIDL Generates (NDK Backend)

For the NDK backend (which vendor code must use), the generated code looks approximately like this:

#### The Interface (abstract class)

```cpp
class IThermalControlService : public ::ndk::ICInterface {
public:
    static const char* descriptor;  // "com.myoem.thermalcontrol.IThermalControlService"
    
    virtual ::ndk::ScopedAStatus getTemperature(float* _aidl_return) = 0;
    virtual ::ndk::ScopedAStatus setFanSpeed(int32_t speed) = 0;
};
```

#### The Proxy — BpThermalControlService (client side)

```cpp
class BpThermalControlService : public ::ndk::BpCInterface<IThermalControlService> {
public:
    explicit BpThermalControlService(const ::ndk::SpAIBinder& binder);
    
    ::ndk::ScopedAStatus getTemperature(float* _aidl_return) override {
        ::ndk::AParcel* _aidl_in = nullptr;
        ::ndk::AParcel* _aidl_out = nullptr;
        
        AParcel_create(&_aidl_in);
        // write arguments (none here)
        
        // TRANSACTION_getTemperature = FIRST_CALL_TRANSACTION + 0 = 1
        AIBinder_transact(asBinder().get(), 
                          FIRST_CALL_TRANSACTION + 0,  // method code
                          &_aidl_in, &_aidl_out, 0);
        
        AParcel_readFloat(_aidl_out, _aidl_return);  // read result
        return ::ndk::ScopedAStatus::ok();
    }
};
```

#### The Stub — BnThermalControlService (server side)

```cpp
class BnThermalControlService : public ::ndk::BnCInterface<IThermalControlService> {
public:
    ::ndk::SpAIBinder createBinder() override;
    
protected:
    ::ndk::SpAIBinder::TransactionCode _aidl_onTransact(
            uint32_t code, const ::ndk::AParcel& in, ::ndk::AParcel& out) {
        
        switch (code) {
            case FIRST_CALL_TRANSACTION + 0: {  // getTemperature
                float _aidl_return;
                ::ndk::ScopedAStatus status = getTemperature(&_aidl_return);
                AParcel_writeStatusHeader(&out, status.get());
                AParcel_writeFloat(&out, _aidl_return);
                return STATUS_OK;
            }
            case FIRST_CALL_TRANSACTION + 1: {  // setFanSpeed
                int32_t speed;
                AParcel_readInt32(&in, &speed);
                ::ndk::ScopedAStatus status = setFanSpeed(speed);
                AParcel_writeStatusHeader(&out, status.get());
                return STATUS_OK;
            }
        }
        return STATUS_UNKNOWN_TRANSACTION;
    }
};
```

Your service class extends `BnThermalControlService` and implements `getTemperature()` and `setFanSpeed()`. The `onTransact()` dispatching is handled for you.

---

### Transaction Codes

Each method gets a unique integer code. AIDL assigns them sequentially starting from `IBinder::FIRST_CALL_TRANSACTION` (= 1). The order is the order methods appear in the `.aidl` file.

```
getTemperature  → code 1
setFanSpeed     → code 2
registerCallback → code 3
```

**This matters for versioning.** If you have a frozen (stable) AIDL interface and add a new method, it must go at the END. If you reorder methods, the codes change and old clients calling the new server will call the wrong method.

---

### The Three AIDL Backends

AIDL can generate code in three backends:

| Backend | Library | Use Case |
|---------|---------|---------|
| `cpp` | `libbinder` | Framework code, system processes |
| `ndk` | `libbinder_ndk` | **Vendor code** — must use this |
| `java` | Android framework | Apps, Java system services |

In your `Android.bp`, you control this:

```bp
aidl_interface {
    name: "com.myoem.thermalcontrol",
    srcs: ["aidl/**/*.aidl"],
    vendor_available: true,
    unstable: true,
    backend: {
        cpp: { enabled: false },    // disable cpp for vendor
        ndk: { enabled: true },     // enable ndk for vendor
        java: { enabled: false },
    },
}
```

Why can't vendor code use `libbinder` (cpp backend)? Because `libbinder` is a framework library, and vendor code cannot link against framework libraries (VNDK rules). `libbinder_ndk` is an LLNDK library — it has a stable C ABI that vendor code can safely link against across Android versions.

---

### Parcelable in AIDL

You can define data structures that cross the Binder boundary:

```aidl
// ThermalStatus.aidl
package com.myoem.thermalcontrol;

parcelable ThermalStatus {
    float temperature;
    int fanSpeed;
    boolean isCritical;
}
```

AIDL generates `writeToParcel()` and `readFromParcel()` implementations automatically.

---

### Callbacks (Binder in Both Directions)

A common pattern is for the client to pass a callback object to the server:

```aidl
interface IThermalCallback {
    oneway void onTemperatureChanged(float temp);
}

interface IThermalControlService {
    void registerCallback(IThermalCallback callback);
}
```

When the client calls `registerCallback(myCallback)`, it passes a `BnThermalCallback` (the client IS the server for this interface). The thermal control service holds a `BpThermalCallback` and calls it when temperature changes. The roles are reversed for the callback interface.

`oneway` means the server fires the callback and doesn't wait for the client to finish handling it. This prevents the server from blocking if the client is slow.

---

### `oneway` Semantics — Important Detail

```aidl
oneway void onTemperatureChanged(float temp);  // non-blocking
void setFanSpeed(int speed);                    // blocking (default)
```

- **Regular call:** Client thread blocks until server's method returns. Like a synchronous function call.
- **`oneway` call:** Client sends the transaction and returns immediately. No reply. Client does NOT block.

`oneway` calls are queued. If the server is busy, they wait in the kernel. They are guaranteed to arrive in order from a single client.

---

### Module 4 — Self-Check Questions

1. AIDL generates `BnXxx` and `BpXxx`. Which one does your service extend, and which one does the client hold?
2. If you have a frozen AIDL interface with 3 methods and you add a new method at position 2 (not the end), what breaks?
3. Why must vendor code use the `ndk` backend instead of the `cpp` backend? What is LLNDK?
4. In your ThermalControl project, if you want the server to notify clients when temperature crosses a threshold, what AIDL pattern would you use?
5. What does `oneway` do? If a server is overloaded and can't process callbacks fast enough, what happens to `oneway` calls — are they dropped or queued?
6. What is the transaction code for the third method defined in an AIDL interface?

---

---

## Module 5: Service Manager Deep Dive

### What is the Service Manager?

The Service Manager is a special Android system process (`/system/bin/servicemanager`) that acts as the **registry** for all named Binder services. It is the DNS of Binder — you register your service by name, and clients look up your service by name.

```
Server registers:  addService("com.myoem.thermalcontrol", myBinder)
Client looks up:   getService("com.myoem.thermalcontrol") → BpBinder
```

---

### Handle 0 — The Bootstrap Problem

There's a chicken-and-egg problem: to talk to any Binder service, you need a Binder reference to it. But to get a Binder reference to any service, you need to talk to the Service Manager. How do you get the Service Manager's reference?

The kernel solves this with a hardcoded convention: **handle 0 always refers to the Service Manager**. Every process can immediately create a `BpBinder(0)` and talk to the Service Manager without any prior registration. This is what `defaultServiceManager()` does:

```cpp
sp<IServiceManager> defaultServiceManager() {
    // BpBinder(0) = proxy to handle 0 = Service Manager
    return interface_cast<IServiceManager>(ProcessState::self()->getContextObject(nullptr));
    // getContextObject(nullptr) returns BpBinder(0)
}
```

The Service Manager itself registers handle 0 with the kernel at startup using a special `ioctl(BINDER_SET_CONTEXT_MGR)` call. Only one process can do this — whoever does it first becomes the context manager.

---

### addService — Registering Your Service

```cpp
// In your server's main.cpp
sp<IThermalControlService> service = new ThermalControlService();
sp<IServiceManager> sm = defaultServiceManager();
sm->addService(String16("com.myoem.thermalcontrol"), service);
```

What happens internally:
1. `sm->addService(...)` marshals the name and your `BBinder*` into a `Parcel`.
2. Sends `BC_TRANSACTION` with handle `0` (Service Manager) and code `ADD_SERVICE_TRANSACTION`.
3. Kernel delivers this to the Service Manager process.
4. Service Manager validates permissions (SELinux check: can this caller add a service with this name?).
5. Service Manager stores the mapping: `"com.myoem.thermalcontrol"` → handle for your `BBinder`.
6. Future callers who ask for this name get a handle to your `BBinder`.

---

### getService vs checkService vs waitForService

```cpp
// getService — returns immediately, null if not registered yet
sp<IBinder> binder = sm->getService(String16("com.myoem.thermalcontrol"));

// checkService — same as getService (non-blocking)
sp<IBinder> binder = sm->checkService(String16("com.myoem.thermalcontrol"));

// waitForService — blocks until the service is registered (AOSP 11+)
sp<IBinder> binder = sm->waitForService<IThermalControlService>(
    String16("com.myoem.thermalcontrol"));
```

**`waitForService` is the recommended pattern for service clients.** Services may not start instantly — the service daemon may not have started yet when your client process starts. `waitForService` registers a "service notification" with the kernel and sleeps until the service becomes available, rather than polling.

---

### service_contexts — SELinux Meets Service Manager

Every service name must have an entry in `service_contexts` (for framework services) or `vndservice_contexts` (for vendor services):

```
# service_contexts or vndservice_contexts
com.myoem.thermalcontrol    u:object_r:thermalcontrol_service:s0
```

This assigns a SELinux type to the service name. The Service Manager checks this when:
- A process tries to `addService` — does this process have `add_service` permission for this type?
- A process tries to `getService` — does this process have `find_service` permission for this type?

```te
# In your .te SELinux policy file
allow thermalcontrold_service thermalcontrold_service:service_manager add;
allow some_client_domain thermalcontrold_service:service_manager find;
```

---

### The Service Manager's Internal Data Structure

The Service Manager maintains a simple map:

```cpp
// Simplified servicemanager internals
struct ServiceEntry {
    String16 name;
    sp<IBinder> binder;
    int uid;        // UID of the registering process
    bool isIsolated;
};
std::map<String16, ServiceEntry> services;
```

When you call `addService`, the Service Manager stores your `BBinder`. When a client calls `getService`, the Service Manager returns a reference to that same `BBinder`. The kernel handles turning that reference into an appropriate handle for the client process.

---

### Listing Services — Useful Debug Commands

```bash
# List all registered Binder services
adb shell service list

# Check if a specific service is registered
adb shell service check com.myoem.thermalcontrol

# Call a service's dump method
adb shell dumpsys com.myoem.thermalcontrol

# List vendor services (vndbinder)
adb shell vndservice list
```

---

### defaultServiceManager() vs getContextObject()

```cpp
// These two are equivalent
sp<IServiceManager> sm = defaultServiceManager();

sp<IBinder> obj = ProcessState::self()->getContextObject(nullptr);
sp<IServiceManager> sm2 = interface_cast<IServiceManager>(obj);
```

`interface_cast<T>` calls `T::asInterface()` which does a `queryLocalInterface()` check (if it's local) or wraps it in a `BpT` proxy. For the Service Manager, it creates a `BpServiceManager(BpBinder(0))`.

---

### Module 5 — Self-Check Questions

1. Why is handle `0` special? How does the kernel know to always route handle-0 transactions to the Service Manager?
2. What is the difference between `getService()` and `waitForService()`? In a daemon that starts at boot, which should you use?
3. If your service is not listed in `service_contexts` (or `vndservice_contexts`), what happens when your daemon tries to call `addService`?
4. `adb shell service list` — look at the output. Which services do you recognize? Are they all Binder services?
5. What does `interface_cast<IServiceManager>()` actually do? What class does it return?
6. In your ThermalControl project, what is the exact service name string used in `addService`? Where must this string also appear?

---

---

## Module 6: Transaction Lifecycle (End-to-End)

### Overview

This module traces a single Binder call from the client's method invocation all the way to the server's method execution and back. Understanding this flow completely is the key to debugging Binder issues.

We'll trace: `client calls getTemperature()` → server executes → client receives result.

---

### Step 1: Client Calls the Proxy Method

```cpp
// Client code
sp<IThermalControlService> service = /* from getService */;
float temp;
service->getTemperature(&temp);
```

The client holds a `BpThermalControlService`. Calling `getTemperature()` enters the AIDL-generated proxy:

```cpp
// BpThermalControlService::getTemperature (generated by AIDL)
::ndk::ScopedAStatus getTemperature(float* _aidl_return) {
    AParcel* in_parcel = nullptr;
    AParcel_create(&in_parcel);
    
    // Write the interface descriptor (sanity check on server side)
    AParcel_writeString(in_parcel, DESCRIPTOR);
    
    // No arguments for getTemperature, so nothing else to write
    
    // BLOCKS HERE waiting for reply
    binder_status_t status = AIBinder_transact(
        asBinder().get(),         // the AIBinder* (wraps BpBinder)
        1,                        // TRANSACTION_getTemperature
        &in_parcel,
        &out_parcel,
        0                         // flags (not oneway)
    );
    
    // Read the result
    AParcel_readFloat(out_parcel, _aidl_return);
}
```

---

### Step 2: IPCThreadState Sends BC_TRANSACTION

`AIBinder_transact` → `BpBinder::transact()` → `IPCThreadState::transact()`:

```cpp
// IPCThreadState writes to its output buffer:
BC_TRANSACTION
binder_transaction_data {
    target.handle = 7,        // handle for thermalcontrold's BBinder
    code = 1,                 // getTemperature
    flags = 0,                // not oneway — we expect a reply
    sender_pid = <client PID>,
    sender_euid = <client UID>,
    data_size = 28,           // size of Parcel data
    data.ptr.buffer = 0x...,  // pointer to Parcel contents
    data.ptr.offsets = 0x..., // no embedded Binders here
}
```

Then `talkWithDriver()` calls `ioctl(BINDER_WRITE_READ)`. The client thread is now **suspended in the kernel**.

---

### Step 3: Kernel Processes the Transaction

Inside `binder_transaction()` in the kernel:

1. Kernel looks up handle `7` in the client's `binder_ref` table → finds the `binder_node` owned by `thermalcontrold`.
2. Kernel finds a free thread in `thermalcontrold`'s thread pool (a thread sleeping in `joinThreadPool()`).
3. **Data copy:** Kernel copies the Parcel data into `thermalcontrold`'s mmap'd receive buffer. (ONE copy.)
4. Kernel adds a `BR_TRANSACTION` to `thermalcontrold`'s thread's pending queue.
5. Kernel wakes up that thread.
6. Kernel puts the client thread to sleep (it sent a non-oneway transaction, so it waits for `BR_REPLY`).

---

### Step 4: Server Thread Wakes Up with BR_TRANSACTION

The server thread (sleeping in `joinThreadPool()`'s `talkWithDriver()`) wakes up:

```cpp
// IPCThreadState::executeCommand(BR_TRANSACTION)
binder_transaction_data tr;
// read tr from the mmap'd buffer

// Find the BBinder this is for
sp<BBinder> binder = reinterpret_cast<BBinder*>(tr.cookie);
// cookie was set to the BBinder* when the node was registered

// Dispatch to the BBinder
binder->transact(tr.code, Parcel(tr.data), &reply, tr.flags);
```

This calls `BnThermalControlService::onTransact()`:

```cpp
// BnThermalControlService::_aidl_onTransact (AIDL-generated)
switch (code) {
    case 1:  // TRANSACTION_getTemperature
        float _aidl_return;
        getTemperature(&_aidl_return);  // calls YOUR implementation!
        AParcel_writeFloat(&out, _aidl_return);
        return STATUS_OK;
}
```

`getTemperature()` is YOUR code in `ThermalControlService.cpp` — reading from sysfs, doing the calculation, returning the value.

---

### Step 5: Server Sends BC_REPLY

After `onTransact()` returns, `IPCThreadState` sends the reply:

```cpp
IPCThreadState::sendReply(reply, 0);
// This writes BC_REPLY + the reply Parcel to the output buffer
// Then calls talkWithDriver()
```

The kernel:
1. Copies the reply data into the client's mmap'd receive buffer.
2. Wakes up the client thread with `BR_REPLY`.
3. The server thread returns to `joinThreadPool()` and sleeps again, ready for next transaction.

---

### Step 6: Client Reads the Reply

The client thread wakes from its `ioctl` with `BR_REPLY`. `waitForResponse()` reads the reply Parcel, extracts the float, and returns it to the AIDL proxy. The proxy writes it to `*_aidl_return`. Your client code now has the temperature value.

---

### The Complete Flow Diagram

```
CLIENT THREAD                    KERNEL                    SERVER THREAD
─────────────                   ────────                   ─────────────
getTemperature()
  BpBinder::transact()
    IPCThreadState::transact()
      write BC_TRANSACTION ──────►
      ioctl(WRITE_READ) ─────────► binder_transaction()
      [BLOCKED]                    1. find server thread
                                   2. copy data (1 copy)
                                   3. wake server thread
                                   4. sleep client thread
                                                         ◄── wakes up
                                                         IPCThreadState::talkWithDriver()
                                                         executeCommand(BR_TRANSACTION)
                                                         BBinder::transact()
                                                         onTransact(code=1)
                                                         getTemperature()
                                                           [read sysfs]
                                                         return 72.5f
                                   ◄── BC_REPLY
                                   binder_transaction()
                                   copy reply data
      wakes up ◄──────────────────
      BR_REPLY
      read float = 72.5f
      return to caller
```

---

### The oneway Case

If the method is `oneway`:
```
CLIENT THREAD                    KERNEL                    SERVER THREAD
─────────────                   ────────                   ─────────────
onTemperatureChanged(72.5f)
  write BC_TRANSACTION (TF_ONE_WAY)
  ioctl ──────────────────────► queues transaction
  [returns immediately]        for server's thread pool
                                                          [some time later]
                                                          wakes up, handles it
                                                          [no reply sent]
```

The client thread does NOT block. The kernel acknowledges receipt with `BR_TRANSACTION_COMPLETE` and lets the client continue immediately.

---

### Nested Calls and Re-entrancy

What if service A calls service B, and service B calls service A?

```
A calls B (A's thread blocks waiting for reply)
  B receives call, starts processing
  B calls back into A
    A's SAME thread wakes up (it was waiting for reply from B, but it can handle incoming calls)
    A processes B's call
    A sends reply to B
  B receives A's reply, finishes processing
  B sends reply to A
A receives reply, continues
```

This works because `IPCThreadState::waitForResponse()` handles `BR_TRANSACTION` while waiting for `BR_REPLY`. The same thread does both. This is called **Binder re-entrancy** and it's essential for avoiding deadlocks.

---

### Module 6 — Self-Check Questions

1. Draw the kernel's role in a Binder transaction. What are the two things the kernel does when it receives `BC_TRANSACTION`?
2. How many times is the Parcel data copied in a complete Binder round-trip (request + reply)? Describe each copy.
3. What happens to the client thread between sending `BC_TRANSACTION` and receiving `BR_REPLY`?
4. What is `tr.cookie` on the server side? How does the kernel know which `BBinder*` to use?
5. If `thermalcontrold` has no threads running (forgot `startThreadPool()`), and a client calls `getTemperature()`, what happens? Does it block forever? Error? Timeout?
6. Explain why `oneway` calls cannot return values. What does the call site look like in AIDL?

---

---

## Module 7: Binder in Java (Framework Side)

### Overview

Everything we've seen so far is the C++ side. The Java side uses the same kernel mechanism, but with a JNI bridge layer. Understanding Java Binder is essential if you're writing system services, managers, or apps that use system APIs.

```
Java Method Call
      ↓
BinderProxy.transact() [java]
      ↓ JNI
android_util_Binder.cpp
      ↓
BpBinder::transact() [C++]
      ↓
IPCThreadState → kernel
```

---

### The Java Class Hierarchy

```java
// android.os package

IBinder          // interface — equivalent to C++ IBinder
├── Binder       // concrete class — equivalent to C++ BBinder
└── BinderProxy  // proxy class — equivalent to C++ BpBinder
```

#### Binder (server side)

```java
public class Binder implements IBinder {
    // Called when a transaction arrives for this object
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        // AIDL-generated subclasses override this
        return super.onTransact(code, data, reply, flags);
    }
    
    // Get the calling UID (reads from kernel, cannot be faked)
    public static final native int getCallingUid();
    public static final native int getCallingPid();
}
```

#### BinderProxy (client side)

```java
final class BinderProxy implements IBinder {
    // mNativeData holds a pointer to the C++ BpBinder
    private long mNativeData;
    
    public boolean transact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        // calls native method → C++ BpBinder::transact()
        return transactNative(code, data, reply, flags);
    }
}
```

---

### AIDL in Java

For a Java backend AIDL interface:

```aidl
// IThermalControlService.aidl
package com.myoem.thermalcontrol;
interface IThermalControlService {
    float getTemperature();
}
```

The generated Java looks like:

```java
public interface IThermalControlService extends android.os.IInterface {
    
    // Server-side stub
    abstract class Stub extends android.os.Binder implements IThermalControlService {
        
        public static IThermalControlService asInterface(android.os.IBinder obj) {
            if (obj == null) return null;
            // Check if it's local (same process)
            android.os.IInterface iin = obj.queryLocalInterface(DESCRIPTOR);
            if (iin != null) return (IThermalControlService) iin;
            // Remote — wrap in proxy
            return new Proxy(obj);
        }
        
        @Override
        public boolean onTransact(int code, android.os.Parcel data,
                android.os.Parcel reply, int flags) throws android.os.RemoteException {
            switch (code) {
                case TRANSACTION_getTemperature: {
                    data.enforceInterface(DESCRIPTOR);
                    float result = this.getTemperature();
                    reply.writeNoException();
                    reply.writeFloat(result);
                    return true;
                }
            }
            return super.onTransact(code, data, reply, flags);
        }
        
        // Client-side proxy (nested class inside Stub!)
        private static class Proxy implements IThermalControlService {
            private android.os.IBinder mRemote;
            
            @Override
            public float getTemperature() throws android.os.RemoteException {
                android.os.Parcel _data = android.os.Parcel.obtain();
                android.os.Parcel _reply = android.os.Parcel.obtain();
                try {
                    _data.writeInterfaceToken(DESCRIPTOR);
                    mRemote.transact(TRANSACTION_getTemperature, _data, _reply, 0);
                    _reply.readException();
                    return _reply.readFloat();
                } finally {
                    _reply.recycle();
                    _data.recycle();
                }
            }
        }
    }
    
    float getTemperature() throws android.os.RemoteException;
}
```

Notice: `Stub.asInterface()` checks if the `IBinder` is local (same process). If it is, it returns the `Binder` object directly — no IPC, no Parcel marshalling, just a normal method call. This optimization is called the **same-process optimization**.

---

### bindService() and ServiceConnection

In apps, you use `bindService()` to connect to a remote service:

```java
Intent intent = new Intent(context, MyService.class);
context.bindService(intent, new ServiceConnection() {
    @Override
    public void onServiceConnected(ComponentName name, IBinder service) {
        // service is a BinderProxy (if remote) or Binder (if same process)
        IThermalControlService ts = IThermalControlService.Stub.asInterface(service);
        // Now you can call ts.getTemperature()
    }
    
    @Override
    public void onServiceDisconnected(ComponentName name) {
        // Service died — handle reconnection
    }
}, Context.BIND_AUTO_CREATE);
```

The `IBinder` passed to `onServiceConnected` is a `BinderProxy` wrapping the service's `Binder` object.

---

### The ANR Risk — Never Bind on Main Thread

`bindService()` itself is asynchronous — it returns immediately and calls `onServiceConnected` later. But **calling methods on the `IBinder`** (i.e., actual Binder transactions) is synchronous — the calling thread blocks until the remote method returns.

```java
// BAD — called on main thread
runOnUiThread(() -> {
    float temp = thermalService.getTemperature(); // BLOCKS MAIN THREAD
    // If thermalcontrold takes > 5 seconds, ANR
});

// GOOD — called on background thread
executorService.execute(() -> {
    float temp = thermalService.getTemperature(); // blocks background thread, OK
    runOnUiThread(() -> updateUI(temp));
});

// BETTER (Kotlin coroutines)
lifecycleScope.launch(Dispatchers.IO) {
    val temp = thermalService.getTemperature()
    withContext(Dispatchers.Main) { updateUI(temp) }
}
```

The main thread has a 5-second ANR timeout for input events. A blocking Binder call on the main thread that takes longer will show "Application Not Responding." Always move Binder calls off the main thread.

---

### RemoteException

Java Binder calls throw `RemoteException` when:
- The remote service has died.
- The transaction failed at the Binder layer.
- The remote threw an exception (declared in the AIDL).

```java
try {
    float temp = thermalService.getTemperature();
} catch (RemoteException e) {
    // Service died or IPC failed
    Log.e(TAG, "Lost connection to thermalcontrold", e);
    thermalService = null; // force re-lookup
}
```

---

### getCallingUid() — Identity in Java

```java
// Inside your service implementation (runs on server side)
@Override
public float getTemperature() {
    int callerUid = Binder.getCallingUid();
    int callerPid = Binder.getCallingPid();
    
    // Check if caller is allowed
    if (callerUid != Process.SYSTEM_UID && !isAllowed(callerUid)) {
        throw new SecurityException("Not allowed: uid=" + callerUid);
    }
    
    return readTemperatureFromSysfs();
}
```

`Binder.getCallingUid()` and `Binder.getCallingPid()` are native methods that read the values the kernel put in the transaction. They reflect the **actual caller's identity**, not a value the caller provided.

---

### Module 7 — Self-Check Questions

1. What is the Java equivalent of C++ `BpBinder`? What is the Java equivalent of C++ `BBinder`?
2. What does `Stub.asInterface()` do when the passed `IBinder` is in the same process? Why is this an optimization?
3. Why does calling a Binder method on the Android main thread risk an ANR? What is the timeout?
4. `RemoteException` — what are the two main reasons it can be thrown?
5. In Java, how do you get the UID of whoever is calling your service method? Why can't the caller fake this?
6. In your ThermalControl project (Phase 3), the Java `ThermalControlManager` calls the native `thermalcontrold`. Draw the call stack from `manager.getTemperature()` all the way to sysfs read.

---

---

## Module 8: Stability & Versioning (AOSP 10+)

### The Problem: The System/Vendor Split

Before Android 8 (Project Treble), vendor code and framework code were deeply intertwined. Updating the framework (for security patches) required revalidating the entire device, including vendor drivers and HALs. This made Android updates slow and expensive.

Treble introduced a strict separation: **system partition** (framework, updated by Google) and **vendor partition** (HALs, drivers, updated by OEM). They must be able to update independently.

But if vendor code links directly against framework libraries and those libraries change their internal APIs, the vendor code breaks. This is the **ABI stability problem**.

---

### VNDK — Vendor NDK

The solution is VNDK (Vendor NDK): a curated set of libraries that the system partition exposes to vendor code with a **stable ABI guarantee**.

```
System partition:
├── libbinder.so           ← Framework only, NOT in VNDK
├── libbinder_ndk.so       ← In VNDK (LLNDK), stable C ABI for vendor
├── libutils.so            ← In VNDK
└── libcutils.so           ← In VNDK

Vendor partition:
├── thermalcontrold        ← Links against libbinder_ndk (VNDK), NOT libbinder
└── libthermalcontrolhal   ← Same rule
```

**This is why vendor code must use `libbinder_ndk` (NDK backend) instead of `libbinder` (cpp backend).** `libbinder` has a C++ ABI that can change between Android versions. `libbinder_ndk` has a stable C ABI (`AIBinder`, `AParcel`, etc.) guaranteed not to change.

---

### Stable AIDL vs Unstable AIDL

#### Unstable AIDL

```bp
aidl_interface {
    name: "com.myoem.thermalcontrol",
    unstable: true,    // ← no versioning, can change freely
    vendor_available: true,
    ...
}
```

Both the client and server are compiled from the same source tree at the same time. You can change the AIDL freely. This is fine for:
- In-vendor interfaces (thermalcontrold ↔ other vendor processes).
- Development/prototyping.

#### Stable AIDL (with versions)

```bp
aidl_interface {
    name: "android.hardware.thermal",
    versions: ["1", "2"],   // frozen versions
    // NOT unstable
    ...
}
```

With stable AIDL:
- **Frozen versions** are immutable. `versions/1/` contains a copy of the AIDL at that freeze point.
- New methods must be added at the END (to preserve transaction codes).
- Old methods cannot be removed or changed.
- Clients compiled against version 1 must still work with a server implementing version 2.

---

### Freezing an Interface

When you're ready to release a stable interface:

```bash
m -j aidl_freeze
# or specifically:
cd vendor/myoem && aidl --checkapi=compatible old_version/ new_version/
```

The build system enforces that the current AIDL is backward-compatible with all frozen versions.

---

### @VintfStability — Crossing the System/Vendor Boundary

If a Binder object needs to cross the system/vendor boundary (system process talks to vendor HAL), the AIDL interface must be annotated with `@VintfStability`:

```aidl
@VintfStability
interface IThermalControlService {
    float getTemperature();
}
```

And in Android.bp:
```bp
aidl_interface {
    name: "android.hardware.thermal",
    stability: "vintf",  // ← required for system/vendor crossing
    ...
}
```

VINTF (Vendor Interface object) is the system that tracks which HAL versions a device supports. It reads `/vendor/etc/vintf/manifest.xml`.

---

### VINTF Manifest

For a HAL registered with the Service Manager to cross the system/vendor boundary, it must be declared in the VINTF manifest:

```xml
<!-- /vendor/etc/vintf/manifest.xml -->
<manifest version="2.0" type="device">
    <hal format="aidl">
        <name>com.myoem.thermalcontrol</name>
        <version>1</version>
        <interface>
            <name>IThermalControlService</name>
            <instance>default</instance>
        </interface>
    </hal>
</manifest>
```

The system's compatibility matrix (`/system/etc/vintf/compatibility_matrix.xml`) declares which HALs it requires. At boot, the framework checks that the device's manifest satisfies the compatibility matrix. If not, the device won't boot.

---

### Why This Matters for Your Projects

For your ThermalControl project within vendor only:
- Both client and server are in `vendor/` — use `unstable: true`.
- Use `libbinder_ndk` (ndk backend) — even for intra-vendor, it's best practice.
- No VINTF manifest needed.

If you were writing a real HAL that the framework needs to talk to:
- Use stable AIDL with frozen versions.
- Add `@VintfStability`.
- Register in VINTF manifest.
- Framework can now call your HAL across the system/vendor boundary safely.

---

### Module 8 — Self-Check Questions

1. What problem does Project Treble solve? Why was the system/vendor split necessary?
2. Why can't vendor code link against `libbinder` directly? What could go wrong across an OTA update?
3. What is the difference between `unstable: true` and a frozen AIDL interface?
4. If you have a frozen AIDL interface at version 1 with methods `getTemperature()` and `setFanSpeed()`, and you need to add `getFanRPM()`, where must you add it?
5. What is `@VintfStability` and when is it required?
6. What file declares which HALs a device provides? Where is it located on the device?

---

---

## Module 9: Death Recipients & Error Handling

### The Problem: What Happens When a Server Dies?

If `thermalcontrold` crashes while your app holds a reference to it and is mid-call:
- The Binder transaction in flight fails with `DEAD_OBJECT`.
- Future calls also fail with `DEAD_OBJECT`.
- You need to detect this and attempt recovery (re-register, re-connect, etc.).

Binder provides two mechanisms: **synchronous error codes** for failures you notice immediately, and **death notifications** for proactive crash detection.

---

### Death Recipients — Proactive Crash Detection

`linkToDeath()` lets you register a callback that fires when a Binder object dies (i.e., when the process that hosts it crashes or exits).

#### C++ Death Recipient

```cpp
class ThermalServiceDeathRecipient : public android::IBinder::DeathRecipient {
public:
    void binderDied(const android::wp<android::IBinder>& who) override {
        LOG(ERROR) << "thermalcontrold died! Attempting reconnect...";
        // Schedule reconnection on a different thread
        // Don't do heavy work here — this runs on a Binder thread
        reconnectToThermalService();
    }
};

// Registration
sp<ThermalServiceDeathRecipient> deathRec = new ThermalServiceDeathRecipient();
sp<IBinder> binder = /* from getService */;
binder->linkToDeath(deathRec);
```

#### NDK Death Recipient

```cpp
// NDK (libbinder_ndk) version
void onDeath(void* cookie) {
    ThermalClient* client = static_cast<ThermalClient*>(cookie);
    LOG(ERROR) << "thermalcontrold died";
    client->reconnect();
}

AIBinder_DeathRecipient* deathRecipient = AIBinder_DeathRecipient_new(onDeath);
AIBinder_linkToDeath(binder, deathRecipient, this /*cookie*/);
```

#### Java Death Recipient

```java
IBinder.DeathRecipient deathRecipient = new IBinder.DeathRecipient() {
    @Override
    public void binderDied() {
        Log.e(TAG, "thermalcontrold died");
        thermalService = null;
        reconnectOnBackgroundThread();
    }
};

IBinder binder = thermalService.asBinder();
binder.linkToDeath(deathRecipient, 0);
```

---

### How Death Notification Works

When a process dies, the kernel marks all `binder_node`s owned by that process as dead. For every `binder_ref` that called `linkToDeath`, the kernel sends `BR_DEAD_BINDER` to the watching process. `IPCThreadState` receives `BR_DEAD_BINDER` and calls `binderDied()` on your `DeathRecipient`.

```
thermalcontrold crashes
     ↓
kernel: marks all binder_nodes for that process as dead
     ↓
kernel: sends BR_DEAD_BINDER to every process with a linkToDeath
     ↓
IPCThreadState::executeCommand(BR_DEAD_BINDER)
     ↓
DeathRecipient::binderDied()  ← your callback fires
```

---

### unlinkToDeath

When you no longer need the death notification (e.g., you're shutting down cleanly):

```cpp
binder->unlinkToDeath(deathRec);
```

Always `unlinkToDeath` before releasing the `DeathRecipient`. Otherwise you may get callbacks after the recipient has been destroyed.

---

### Error Handling — Binder Error Codes

Binder transactions can fail with several error codes:

| Error Code | Meaning |
|-----------|---------|
| `NO_ERROR` / `OK` | Success |
| `DEAD_OBJECT` | The remote Binder object is dead (process crashed) |
| `FAILED_TRANSACTION` | Transaction failed (e.g., parcel too large, kernel error) |
| `BAD_VALUE` | Invalid argument |
| `PERMISSION_DENIED` | SELinux or UID check failed |
| `UNKNOWN_TRANSACTION` | The server doesn't know this transaction code (version mismatch?) |
| `NAME_NOT_FOUND` | Service not found in Service Manager |

In C++ (NDK):

```cpp
::ndk::ScopedAStatus status = thermalService->getTemperature(&temp);
if (!status.isOk()) {
    if (status.getExceptionCode() == EX_DEAD_OBJECT) {
        // Server died
    } else {
        LOG(ERROR) << "getTemperature failed: " << status.getDescription();
    }
}
```

In Java:

```java
try {
    float temp = thermalService.getTemperature();
} catch (RemoteException e) {
    // Check if it's a dead object
    if (e instanceof DeadObjectException) {
        // server died
    }
}
```

---

### Ping Binder

A lightweight way to check if a remote Binder is still alive:

```cpp
status_t status = binder->pingBinder();
if (status == DEAD_OBJECT) {
    // Server is dead
}
```

`pingBinder()` sends a special `PING_TRANSACTION` to the remote Binder. If it gets a response, the server is alive. If it gets `DEAD_OBJECT`, the server has died.

---

### The Reconnection Pattern

A robust client should implement reconnection:

```cpp
class ThermalClient {
    sp<IThermalControlService> mService;
    std::mutex mMutex;
    
    void ensureConnected() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mService == nullptr) {
            sp<IBinder> binder = defaultServiceManager()->waitForService(
                String16("com.myoem.thermalcontrol"));
            mService = interface_cast<IThermalControlService>(binder);
            binder->linkToDeath(mDeathRecipient);
        }
    }
    
    float getTemperature() {
        ensureConnected();
        float temp;
        ::ndk::ScopedAStatus s = mService->getTemperature(&temp);
        if (!s.isOk()) {
            mService = nullptr;  // force reconnect next time
            return -1.0f;
        }
        return temp;
    }
    
    void onServiceDied() {
        std::lock_guard<std::mutex> lock(mMutex);
        mService = nullptr;  // next call will reconnect
    }
};
```

---

### Module 9 — Self-Check Questions

1. What is the difference between a `DEAD_OBJECT` error from `transact()` and a `binderDied()` callback? Can you get both for the same death event?
2. What kernel mechanism delivers death notifications? What BR_ command is involved?
3. Why should `binderDied()` not do heavy work (like reconnection logic)? What thread does it run on?
4. `pingBinder()` checks if a Binder is alive. Is it appropriate to call `pingBinder()` before every transaction? What would be the performance impact?
5. If you register `linkToDeath` on a `BpBinder` and then the server process crashes, does `binderDied()` fire immediately or with a delay?
6. In your ThermalControl project, if `thermalcontrold` crashes while `thermalcontrol_client` is in the middle of `getTemperature()`, what error does the client receive?

---

---

## Module 10: SELinux & Binder Security

### SELinux and Mandatory Access Control

Android uses SELinux (Security Enhanced Linux) in enforcing mode. Every process has a **domain** label, and every file/socket/service has a **type** label. The SELinux policy defines which domains can do what to which types.

Without SELinux policies, Binder is wide open — any process could call any service. SELinux adds mandatory access control on top of the UID/PID identity system Binder already provides.

---

### How Binder Calls Are Checked

For a Binder call to succeed, TWO checks must pass:

1. **Kernel check:** Does the calling process's domain have `binder_call` permission on the server's domain?
2. **Service Manager check (for named services):** Does the calling domain have `find_service` permission for this service type?

```te
# 1. Allow thermalcontrol_client to call thermalcontrold via Binder
binder_call(thermalcontrol_client, thermalcontrold)

# 2. Allow thermalcontrol_client to find the service in ServiceManager
allow thermalcontrol_client thermalcontrold_service:service_manager find;
```

The `binder_call(A, B)` macro expands to:
```te
allow A B:binder { call transfer };
allow B A:binder transfer;
```

---

### The Full Chain of SELinux Checks for a Binder Call

Let's trace `thermalcontrol_client` calling `thermalcontrold`:

```
1. thermalcontrol_client calls getService("com.myoem.thermalcontrol")
   SELinux check: allow thermalcontrol_client thermalcontrold_service:service_manager find;
   
2. Client gets back the IBinder and calls transact()
   Kernel check: allow thermalcontrol_client thermalcontrold:binder call;
   
3. thermalcontrold receives the call and processes it
   (optional) Service can check Binder.getCallingUid() in code for additional checks
```

If any of these checks fail:
- A `avc: denied` message appears in logcat.
- The operation fails with `PERMISSION_DENIED`.

---

### SELinux Policy Files for a Vendor Service

For your `thermalcontrold`, you need these files:

#### `thermalcontrold.te` — Domain policy
```te
# Declare the domain
type thermalcontrold, domain;

# It runs as a vendor daemon
vndbinder_use(thermalcontrold)

# Allow it to read/write sysfs thermal files
allow thermalcontrold sysfs_thermal:file { read open };

# Allow it to read/write sysfs hwmon files (fan control)
allow thermalcontrold sysfs:file { read write open getattr };

# Allow it to register its service in vndbinder service manager
allow thermalcontrond thermalcontrold_service:service_manager add;
```

#### `file_contexts` — Label for the binary
```
/vendor/bin/thermalcontrold    u:object_r:thermalcontrold_exec:s0
```

#### `service_contexts` (or `vndservice_contexts`) — Label for the service name
```
com.myoem.thermalcontrol    u:object_r:thermalcontrold_service:s0
```

#### `thermalcontrold_client.te` — Client domain policy
```te
type thermalcontrold_client, domain;
vndbinder_use(thermalcontrold_client)

# Allow finding the service
allow thermalcontrold_client thermalcontrold_service:service_manager find;

# Allow Binder communication
binder_call(thermalcontrold_client, thermalcontrold)
```

---

### Diagnosing SELinux Denials

```bash
# See all AVC denials (since boot)
adb logcat -d | grep "avc: denied"

# See denials in real-time
adb logcat | grep avc

# Example denial:
# avc: denied { find } for pid=4521 uid=1000
#   scontext=u:r:system_server:s0
#   tcontext=u:object_r:thermalcontrold_service:s0
#   tclass=service_manager

# This tells you:
# - system_server tried to find a service
# - The service has type thermalcontrold_service
# - The find permission was denied
# Fix: add allow system_server thermalcontrold_service:service_manager find;
```

---

### getCallingUid() / getCallingPid() — Runtime Identity Checks

In addition to SELinux policy, your service can check the caller's identity in code:

```cpp
// In your service implementation (C++ NDK)
::ndk::ScopedAStatus ThermalControlService::setFanSpeed(int32_t speed) {
    uid_t callerUid = AIBinder_getCallingUid();
    
    // Only allow system (UID 1000) or root (UID 0) to set fan speed
    if (callerUid != AID_SYSTEM && callerUid != AID_ROOT) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_SECURITY);
    }
    
    return setFanSpeedInternal(speed);
}
```

**Key point:** `getCallingUid()` reads the UID from the kernel-provided transaction metadata. The caller cannot fake this value. It is the actual UID of the calling process as determined by the kernel.

---

### SELinux Contexts — How Labels Are Assigned

Every process starts with the label from its executable file's `file_contexts` entry:

```
/vendor/bin/thermalcontrold    u:object_r:thermalcontrold_exec:s0
```

When the kernel executes this binary, it transitions the process's domain from the parent domain to `thermalcontrold` (via a `domain_auto_trans` or explicit transition rule):

```te
# Domain transition rule
domain_auto_trans(init, thermalcontrold_exec, thermalcontrold)
```

This means: when a process in the `init` domain executes a file labeled `thermalcontrold_exec`, the new process runs in the `thermalcontrold` domain.

---

### Permissive Mode — Useful for Development

During development, you can put a specific domain in permissive mode (denials are logged but not enforced):

```te
# In thermalcontrold.te — DEVELOPMENT ONLY, never ship
permissive thermalcontrold;
```

Or globally (for debugging):
```bash
adb shell setenforce 0   # permissive globally
adb shell setenforce 1   # enforcing globally
```

**Never ship a device with SELinux in permissive mode** — it defeats the entire security model.

---

### Module 10 — Self-Check Questions

1. What are the two SELinux checks that must pass for a Binder call to succeed? At what layer does each happen?
2. What does the `binder_call(A, B)` macro expand to? Write out the two `allow` rules.
3. You see this in logcat: `avc: denied { call } for scontext=u:r:my_client:s0 tcontext=u:r:thermalcontrold:s0 tclass=binder`. What SELinux rule is missing?
4. Why is `getCallingUid()` reliable as a security check? What would happen if the kernel didn't fill this in?
5. What is the difference between `service_contexts` and `vndservice_contexts`? When do you use each?
6. In permissive mode, do SELinux denials get logged? Can a shipping device run in permissive mode?

---

---

## Module 11: Advanced Topics

### 1. Binder Memory Management — RefBase, sp<>, wp<>

Binder objects use Android's reference counting, not `std::shared_ptr`. The root class is `RefBase`.

```cpp
class RefBase {
    mutable std::atomic<int32_t> mStrong;  // strong ref count
    mutable std::atomic<int32_t> mWeak;    // weak ref count
    
    void incStrong(const void* id);
    void decStrong(const void* id);
    // When strong count hits 0 → object destroyed
    // When weak count hits 0 → control block freed
};
```

The kernel and userspace together manage cross-process reference counting:
- When you create a `BpBinder(handle)`, the userspace sends `BC_INCREFS` + `BC_ACQUIRE` to the kernel.
- When the `BpBinder` is destroyed, userspace sends `BC_RELEASE` + `BC_DECREFS`.
- When the kernel's strong ref count hits 0, it sends `BR_RELEASE` to the server process — which can then destroy the `BBinder`.

This ensures a Binder object is not destroyed while any client still holds a reference, even across processes.

---

### 2. Shared Memory Over Binder — Passing Large Data

Binder is optimized for small data (a few KB). For large data (images, audio buffers), you don't put the data in the Parcel — you use shared memory and pass a file descriptor.

```cpp
// Server allocates shared memory
sp<MemoryHeapBase> heap = new MemoryHeapBase(1024 * 1024); // 1MB
sp<MemoryBase> mem = new MemoryBase(heap, 0, 1024 * 1024);

// Server fills it with data
uint8_t* ptr = static_cast<uint8_t*>(mem->pointer());
memcpy(ptr, bigData, bigDataSize);

// Server passes it over Binder — only the fd is transferred!
reply.writeStrongBinder(mem->asBinder());
```

The client receives the `IMemory` reference and can `mmap` the same physical pages:
```cpp
sp<IMemory> mem = interface_cast<IMemory>(data.readStrongBinder());
void* ptr = mem->pointer(); // directly maps the server's memory
```

Only the file descriptor crosses the Binder boundary. The actual data is shared memory — zero copies.

---

### 3. File Descriptors Over Binder

Binder can pass open file descriptors from one process to another. The kernel handles translating the fd number between processes (fd numbers are process-local).

```cpp
// Sending an fd
Parcel data;
data.writeFileDescriptor(fd);  // or writeParcelFileDescriptor for ownership transfer

// Receiving an fd
int receivedFd = data.readFileDescriptor();
// This fd is valid in the receiving process, points to the same kernel file description
```

This is how Android passes camera buffers, GPU surfaces (Gralloc), and audio streams between processes — as file descriptors to shared buffers.

---

### 4. hwbinder vs vndbinder vs binder — Three Domains

Android maintains three separate Binder contexts (three kernel devices):

| Device | Domain | Used For | Service Manager |
|--------|--------|---------|----------------|
| `/dev/binder` | Framework | Apps ↔ System Services | `defaultServiceManager()` |
| `/dev/hwbinder` | HW HAL | Framework ↔ HIDL HALs | `android::hardware::defaultServiceManager()` |
| `/dev/vndbinder` | Vendor | Vendor ↔ Vendor Services | `android::vndservicemanager` |

These three are completely separate. A Binder object on `/dev/binder` cannot be passed to a process that opened `/dev/hwbinder`. They don't share handle namespaces or service registries.

Your vendor code (`thermalcontrold`) uses `/dev/vndbinder`. That's why you use `vndservice list` instead of `service list`, and why your `vndservice_contexts` file is separate from `service_contexts`.

---

### 5. Reading the Kernel Source — Key Functions

If you want to go really deep, these are the key functions in `drivers/android/binder.c`:

```
binder_ioctl()              ← entry point for all ioctl calls
binder_ioctl_write_read()   ← handles BINDER_WRITE_READ
binder_thread_write()       ← processes BC_ commands
binder_thread_read()        ← generates BR_ commands
binder_transaction()        ← the heart — processes BC_TRANSACTION
binder_get_node()           ← looks up a binder_node by pointer
binder_get_ref()            ← looks up a binder_ref by handle
binder_alloc_buf()          ← allocates from the mmap'd buffer
```

Reading `binder_transaction()` is worth the time. It shows exactly what the kernel does: validates the transaction, finds the target, copies the data, adjusts embedded Binder object pointers, and wakes the target thread.

---

### 6. Binder Transactions — Size Limits

Binder has a per-transaction size limit (default 1MB per transaction, configurable). This is why large data should go through shared memory (see above), not directly in the Parcel.

```
Transaction size limit: ~1MB (BINDER_VM_SIZE / 2 in practice)
Total mmap buffer per process: 1MB (apps) to 8MB (system processes)
```

If you exceed the limit:
- `BC_TRANSACTION` fails with `BR_FAILED_REPLY`.
- `transact()` returns `FAILED_TRANSACTION`.
- In Java, `RemoteException` with `FAILED_TRANSACTION`.

---

### 7. Binder Thread Count and Saturation

Every process has a thread pool for handling incoming Binder calls. The default maximum is 15 threads (plus the main thread). When all threads are busy:

```
All 15 Binder threads busy handling calls
New call arrives
Kernel: can't wake a thread (all busy)
Client: blocks indefinitely
Eventually: ANR (if on main thread) or deadlock
```

You can increase the thread count for high-traffic services:
```cpp
ProcessState::self()->setThreadPoolMaxThreadCount(32);
```

But this costs memory (each thread has a stack) and CPU (context switching). The right answer is usually to make your service methods fast so threads are freed quickly.

---

### Module 11 — Self-Check Questions

1. When a `BpBinder` is created for handle 7, what BC_ commands does the userspace send to the kernel? What do they accomplish?
2. Why is Binder unsuitable for passing a 100MB video frame as Parcel data? What is the right approach?
3. `thermalcontrold` registers on `/dev/vndbinder`. Can a framework Java service (using `/dev/binder`) directly call `thermalcontrold`? If not, how would you bridge them?
4. What happens if all threads in a server's Binder thread pool are blocked on outgoing calls and new incoming calls arrive?
5. What is the default Binder transaction size limit? If your Parcel exceeds it, what error do you see?
6. Describe the cross-process reference counting flow: from the moment a client gets a `BpBinder` to the moment it's destroyed.

---

---

## Final Review — Putting It All Together

Here is the complete picture of a Binder call, from AIDL interface to kernel and back:

```
┌──────────────────────────────────────────────────────────────────────┐
│                         CLIENT PROCESS                               │
│  IThermalControlService (AIDL interface)                             │
│       ↓ (cast via Stub::asInterface)                                 │
│  BpThermalControlService (AIDL Proxy)                                │
│       ↓ (calls AIBinder_transact / BpBinder::transact)               │
│  IPCThreadState::transact()  [THREAD BLOCKS]                         │
│       ↓ (ioctl BINDER_WRITE_READ with BC_TRANSACTION)                │
└──────────────────────────────┬───────────────────────────────────────┘
                               │ /dev/vndbinder
┌──────────────────────────────▼───────────────────────────────────────┐
│                        LINUX KERNEL                                  │
│  binder_transaction()                                                │
│    1. Look up handle → find binder_node (server's BBinder*)          │
│    2. Copy Parcel data into server's mmap region (1 copy)            │
│    3. Fill sender_euid/sender_pid from process credentials           │
│    4. Wake a server thread with BR_TRANSACTION                       │
│    5. Put client thread to sleep                                     │
└──────────────────────────────┬───────────────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────────┐
│                         SERVER PROCESS                               │
│  IPCThreadState::joinThreadPool() [wakes up]                         │
│       ↓ executeCommand(BR_TRANSACTION)                               │
│  BBinder::transact()                                                 │
│       ↓                                                              │
│  BnThermalControlService::_aidl_onTransact() [AIDL Stub]             │
│       ↓ (switch on transaction code, unmarshal arguments)            │
│  ThermalControlService::getTemperature() [YOUR CODE]                 │
│       ↓ (read from /sys/class/thermal/thermal_zone0/temp)            │
│  return 72500 → 72.5°C                                               │
│       ↑ (marshal reply into Parcel)                                  │
│  IPCThreadState sends BC_REPLY                                       │
└──────────────────────────────┬───────────────────────────────────────┘
                               │ kernel copies reply to client's mmap
┌──────────────────────────────▼───────────────────────────────────────┐
│                      CLIENT PROCESS (resumes)                        │
│  IPCThreadState reads BR_REPLY                                       │
│  BpThermalControlService reads float from reply Parcel               │
│  Returns 72.5f to caller                                             │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Resources for Further Study

| Resource | What You'll Learn |
|----------|-----------------|
| `frameworks/native/libs/binder/` | Full libbinder source — ProcessState, IPCThreadState, Parcel |
| `frameworks/native/cmds/servicemanager/` | Service Manager implementation |
| `drivers/android/binder.c` (kernel) | Kernel driver — the ground truth |
| `system/tools/aidl/` | AIDL compiler source — how .aidl → generated code |
| Dianne Hackborn's original Binder overview (AOSP wiki) | Historical context from the original author |
| `adb shell /vendor/bin/binderThroughputTest` | Binder performance benchmarks on your device |

---

*This guide was written for an AOSP developer learning Binder from ground up, using the ThermalControl project on Raspberry Pi 5 / Android 15 as the reference implementation.*
