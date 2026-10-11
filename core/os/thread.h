/**************************************************************************/
/*  thread.h                                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "platform_config.h" // IWYU pragma: keep. Can override the implementation.

// Define PLATFORM_THREAD_OVERRIDE in your platform's `platform_config.h`
// to use a custom Thread implementation defined in `platform/[your_platform]/platform_thread.h`.
// Overriding the Thread implementation is required in some proprietary platforms.

#ifdef PLATFORM_THREAD_OVERRIDE

#include "platform_thread.h"

#else

#include "core/typedefs.h"

#ifdef THREADS_ENABLED

#include "core/templates/safe_refcount.h"

#include <new> // IWYU pragma: keep // For hardware interference size.

#ifdef MINGW_ENABLED
#define MINGW_STDTHREAD_REDUNDANCY_WARNING
#include <thirdparty/mingw-std-threads/mingw.thread.h>
#define THREADING_NAMESPACE mingw_stdthread
#else
#include <thread>
#define THREADING_NAMESPACE std
#endif

class String;

class Thread {
public:
	typedef void (*Callback)(void *p_userdata);

	typedef uint64_t ID;

	enum : ID {
		UNASSIGNED_ID = 0,
		MAIN_ID = 1
	};

	enum Priority {
		PRIORITY_LOW,
		PRIORITY_NORMAL,
		PRIORITY_HIGH
	};

	struct Settings {
		Priority priority;
		Settings() { priority = PRIORITY_NORMAL; }
	};

	struct PlatformFunctions {
		Error (*set_name)(const String &) = nullptr;
		void (*set_priority)(Thread::Priority) = nullptr;
		void (*init)() = nullptr;
		void (*wrapper)(Thread::Callback, void *) = nullptr;
		void (*term)() = nullptr;
	};

#if defined(__cpp_lib_hardware_interference_size) && !defined(ANDROID_ENABLED) // This would be OK with NDK >= 26.
	GODOT_GCC_WARNING_PUSH_AND_IGNORE("-Winterference-size")
	static constexpr size_t CACHE_LINE_BYTES = std::hardware_destructive_interference_size;
	GODOT_GCC_WARNING_POP
#else
	// At a negligible memory cost, we use a conservatively high value.
	static constexpr size_t CACHE_LINE_BYTES = 128;
#endif

private:
	friend class Main;

	static PlatformFunctions platform_functions;

	ID id = UNASSIGNED_ID;

	static inline SafeFlag is_main_thread_assigned{ false };
	// 取号器：改用惰性初始化的函数内静态量，而不是静态存储期的对象。
	//
	// ⚠️ 为什么（2026-10-11 实测根因）：原写法是
	//     SafeNumeric<uint64_t> Thread::id_counter(1);
	//     thread_local ID Thread::caller_id = Thread::id_counter.increment();
	//   两者都是**动态初始化**（SafeNumeric 的构造函数调用 std::atomic::store，非 constexpr；
	//   C++17 下 std::atomic 的构造函数同样不是 constexpr），而跨编译单元的静态初始化
	//   顺序在 C++ 中是**不确定**的。顺序一旦反过来，caller_id 的初始化器就会读到
	//   id_counter 仍为 0 的时刻，increment() 于是返回 1 = MAIN_ID —— 主线程「天生」就是
	//   主线程，make_main_thread() 走 `if (caller_id == MAIN_ID) return;` 提前返回，
	//   is_main_thread_assigned 永不置位，最终在 release_main_thread() 的断言处崩溃。
	//   profiler=tracy 构建可稳定复现（Tracy 的静态初始化改变了该顺序）；
	//   此外动态初始化还会把计数器重置回 1，造成线程 ID 重复。
	//
	//   函数内静态量由语言保证「首次使用时才初始化」，因此 increment() 必然先看到 1，
	//   首个 ID 稳定为 2；同时也消除计数器被重置的问题。
	static SafeNumeric<uint64_t> &_id_counter();
	static thread_local ID caller_id;
	THREADING_NAMESPACE::thread thread;

	static void callback(ID p_caller_id, const Settings &p_settings, Thread::Callback p_callback, void *p_userdata);

public:
	static void _set_platform_functions(const PlatformFunctions &p_functions);

	_FORCE_INLINE_ static void yield() { std::this_thread::yield(); }

	_FORCE_INLINE_ ID get_id() const { return id; }
	// get the ID of the caller thread
	_FORCE_INLINE_ static ID get_caller_id() {
		return caller_id;
	}
	// get the ID of the main thread
	_FORCE_INLINE_ static ID get_main_id() { return MAIN_ID; }

	_FORCE_INLINE_ static bool is_main_thread() { return caller_id == MAIN_ID; } // Gain a tiny bit of perf here because there is no need to validate caller_id here, because only main thread will be set as 1.
	static void make_main_thread();
	static void release_main_thread();

	static Error set_name(const String &p_name);

	ID start(Thread::Callback p_callback, void *p_user, const Settings &p_settings = Settings());
	bool is_started() const;
	///< waits until thread is finished, and deallocates it.
	void wait_to_finish();

	~Thread();
};

#else // No threads.

class String;

class Thread {
public:
	typedef void (*Callback)(void *p_userdata);

	typedef uint64_t ID;

	static constexpr size_t CACHE_LINE_BYTES = sizeof(void *);

	enum : ID {
		UNASSIGNED_ID = 0,
		MAIN_ID = 1
	};

	enum Priority {
		PRIORITY_LOW,
		PRIORITY_NORMAL,
		PRIORITY_HIGH
	};

	struct Settings {
		Priority priority;
		Settings() { priority = PRIORITY_NORMAL; }
	};

	struct PlatformFunctions {
		Error (*set_name)(const String &) = nullptr;
		void (*set_priority)(Thread::Priority) = nullptr;
		void (*init)() = nullptr;
		void (*wrapper)(Thread::Callback, void *) = nullptr;
		void (*term)() = nullptr;
	};

private:
	friend class Main;

	static PlatformFunctions platform_functions;

public:
	static void _set_platform_functions(const PlatformFunctions &p_functions);

	_FORCE_INLINE_ ID get_id() const { return 0; }
	_FORCE_INLINE_ static ID get_caller_id() { return MAIN_ID; }
	_FORCE_INLINE_ static ID get_main_id() { return MAIN_ID; }

	_FORCE_INLINE_ static bool is_main_thread() { return true; }
	static void make_main_thread() {}
	static void release_main_thread() {}

	static Error set_name(const String &p_name) { return ERR_UNAVAILABLE; }

	void start(Thread::Callback p_callback, void *p_user, const Settings &p_settings = Settings()) {}
	bool is_started() const { return false; }
	void wait_to_finish() {}
};

#endif // THREADS_ENABLED

#endif // PLATFORM_THREAD_OVERRIDE
