/*
 * Small platform layer for the FidelityFX Vulkan frame-interpolation provider.
 * The upstream provider uses Win32 synchronization and timing primitives on
 * Windows. Keep those APIs on Windows, and provide equivalent semantics for
 * the Linux Vulkan build without pretending that Linux has a Win32 surface.
 */
#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <synchapi.h>
#else

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <thread>

using DWORD = uint32_t;
using LPVOID = void *;
using LPTHREAD_START_ROUTINE = DWORD (*)(LPVOID);
using UINT64 = uint64_t;

struct LARGE_INTEGER {
    int64_t QuadPart;
};

struct q2_fsr3_event {
    std::mutex mutex;
    std::condition_variable condition;
    bool manual_reset;
    bool signaled;
};

struct q2_fsr3_thread {
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::thread worker;
};

struct q2_fsr3_handle {
    enum class kind { event, thread } type;
    q2_fsr3_event *event = nullptr;
    q2_fsr3_thread *thread = nullptr;
};

using HANDLE = q2_fsr3_handle *;

constexpr DWORD WAIT_OBJECT_0 = 0;
constexpr DWORD WAIT_TIMEOUT = 258;
constexpr DWORD INFINITE = 0xffffffffu;
constexpr int THREAD_PRIORITY_HIGHEST = 2;
constexpr int FALSE = 0;
constexpr int TRUE = 1;

inline HANDLE CreateEvent(void *, int manual_reset, int initial_state,
                          const char *)
{
    auto *event = new q2_fsr3_event{
        {}, {}, manual_reset != 0, initial_state != 0};
    auto *handle = new q2_fsr3_handle{q2_fsr3_handle::kind::event, event,
                                      nullptr};
    return handle;
}

inline HANDLE CreateThread(void *, size_t, LPTHREAD_START_ROUTINE routine,
                           LPVOID parameter, DWORD, DWORD *)
{
    auto *thread = new q2_fsr3_thread;
    auto *handle = new q2_fsr3_handle{q2_fsr3_handle::kind::thread, nullptr,
                                      thread};
    try {
        thread->worker = std::thread([thread, routine, parameter]() {
            if (routine)
                routine(parameter);
            {
                std::lock_guard<std::mutex> lock(thread->mutex);
                thread->finished = true;
            }
            thread->condition.notify_all();
        });
    } catch (...) {
        delete handle;
        delete thread;
        return nullptr;
    }
    return handle;
}

inline DWORD WaitForSingleObject(HANDLE handle, DWORD timeout)
{
    if (!handle)
        return WAIT_TIMEOUT;

    if (handle->type == q2_fsr3_handle::kind::event) {
        auto *event = handle->event;
        std::unique_lock<std::mutex> lock(event->mutex);
        const auto ready = [event]() { return event->signaled; };
        bool signaled = false;
        if (timeout == INFINITE)
            event->condition.wait(lock, ready), signaled = true;
        else
            signaled = event->condition.wait_for(
                lock, std::chrono::milliseconds(timeout), ready);
        if (signaled && !event->manual_reset)
            event->signaled = false;
        return signaled ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }

    auto *thread = handle->thread;
    std::unique_lock<std::mutex> lock(thread->mutex);
    const auto ready = [thread]() { return thread->finished; };
    const bool finished = timeout == INFINITE
        ? (thread->condition.wait(lock, ready), true)
        : thread->condition.wait_for(
            lock, std::chrono::milliseconds(timeout), ready);
    lock.unlock();
    if (finished && thread->worker.joinable())
        thread->worker.join();
    return finished ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

inline int SetEvent(HANDLE handle)
{
    if (!handle || handle->type != q2_fsr3_handle::kind::event)
        return 0;
    auto *event = handle->event;
    {
        std::lock_guard<std::mutex> lock(event->mutex);
        event->signaled = true;
    }
    if (event->manual_reset)
        event->condition.notify_all();
    else
        event->condition.notify_one();
    return 1;
}

inline int ResetEvent(HANDLE handle)
{
    if (!handle || handle->type != q2_fsr3_handle::kind::event)
        return 0;
    auto *event = handle->event;
    std::lock_guard<std::mutex> lock(event->mutex);
    event->signaled = false;
    return 1;
}

inline int CloseHandle(HANDLE handle)
{
    if (!handle)
        return 0;
    if (handle->type == q2_fsr3_handle::kind::thread) {
        WaitForSingleObject(handle, INFINITE);
        delete handle->thread;
    } else {
        delete handle->event;
    }
    delete handle;
    return 1;
}

inline int SetThreadPriority(HANDLE, int)
{
    return 1;
}

inline int SetThreadDescription(HANDLE, const wchar_t *)
{
    return 0;
}

inline int QueryPerformanceCounter(LARGE_INTEGER *value)
{
    if (!value)
        return 0;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    value->QuadPart = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    return 1;
}

inline int QueryPerformanceFrequency(LARGE_INTEGER *value)
{
    if (!value)
        return 0;
    value->QuadPart = 1000000000ll;
    return 1;
}

using CRITICAL_SECTION = std::recursive_mutex;

inline void InitializeCriticalSection(CRITICAL_SECTION *) {}
inline void DeleteCriticalSection(CRITICAL_SECTION *) {}
inline void EnterCriticalSection(CRITICAL_SECTION *section) { section->lock(); }
inline void LeaveCriticalSection(CRITICAL_SECTION *section) { section->unlock(); }

#define WINAPI
#define TEXT(value) value

#endif
