#include <time.h>
#include <sys/time.h>
#include <sched.h>
#include <sys/mman.h>
#include <iostream>
#include <pthread.h>

#if __linux__
	#include <sys/capability.h>
#endif

#include "RealtimeNative.h"
#include "Utils.hpp"
#include "Thread.h"

const int SCHED_POLICY = SCHED_RR;

#define NSEC_PER_SEC 1000000000



JavaVM* globalVirtualMachine;

void* run(void* threadPtr)
{
	Thread* thread = (Thread*) threadPtr;
	JNIEnv* env = getEnv(globalVirtualMachine);

	if(env == 0)
	{
		// Cannot throw a runtime exception because we don't have an env
		std::cerr << "Cannot load env" << std::endl;
		return (void*)-1;
	}

	if(thread->periodic && thread->setTriggerToClock)
	{
		JNIassert(env, clock_gettime(CLOCK_MONOTONIC, &thread->nextTrigger) == 0);
		thread->setTriggerToClock = false;
	}

	env->CallVoidMethod(thread->javaThread, thread->methodID);

	env->DeleteGlobalRef(thread->javaThread);
	delete thread;

	releaseEnv(globalVirtualMachine);

	thread->retVal = 0;
	pthread_exit(&thread->retVal);
	return NULL;
}


/**
 * Call mlockall to avoid paging memory
 */
JNIEXPORT void JNICALL Java_us_ihmc_realtime_RealtimeNative_mlockall(JNIEnv* env, jclass klass)
{
#if __linux__
	struct __user_cap_header_struct cap_header_data;
	cap_header_data.pid = getpid();
	cap_header_data.version = _LINUX_CAPABILITY_VERSION;

	struct __user_cap_data_struct cap_data;
	capget(&cap_header_data, &cap_data);
	if((cap_data.effective & CAP_IPC_LOCK) != CAP_IPC_LOCK)
	{
//		throwRuntimeException(env, "Cannot lock memory, expect page faults. Run as root");
		std::cerr << "Cannot lock memory, expect page faults. Run as root" << std::endl;
	}
	else
	{
		JNIassert(env, mlockall(MCL_CURRENT|MCL_FUTURE) == 0);
	}
#else
	std::cerr << "Not locking memory on non-linux OS" << std::endl;
#endif
}

/**
 * Register VM to call back to target runnable
 */
JNIEXPORT void JNICALL Java_us_ihmc_realtime_RealtimeNative_registerVM
  (JNIEnv* env, jclass klass)
{
	JNIassert(env, env->GetJavaVM(&globalVirtualMachine) == 0);
}


/**
 * Create a new thread, but do not start
 *
 * @param target Target classes, needs to define a run() method
 * @param priority System priority for thread
 * @param periodic True if thread is periodic
 * @param startOnClock True if the thread has to start at startSeconds + startNanoseconds
 * @param startSeconds Start time relative to MONOTONIC clock, seconds part
 * @param startNanoseconds Start time relative to MONOTONIC clock, nanoseconds part
 * @param periodSeconds Period, seconds part
 * @param periodNanoseconds Period, nanoseconds part
 *
 * @return 64bit pointer to Thread object
 */
JNIEXPORT jlong JNICALL Java_us_ihmc_realtime_RealtimeNative_createThread(JNIEnv* env, jclass klass,
		jobject target, jint priority, jboolean periodic, jboolean startOnClock, jlong startSeconds,
		jlong startNanoseconds, jlong periodSeconds, jlong periodNanoseconds)
{

	// Create thread object
	Thread* thread = new Thread();
	thread->priority = priority;
	thread->periodic = periodic;
	if(periodic)
	{
		if(startOnClock)
		{
			thread->setTriggerToClock = false;
			thread->nextTrigger.tv_sec = startSeconds;
			thread->nextTrigger.tv_nsec = startNanoseconds;

			// Normalize nextTrigger
			tsnorm(&thread->nextTrigger);
		}
		else
		{
			thread->setTriggerToClock = true;
		}

		thread->period.tv_sec = periodSeconds;
		thread->period.tv_nsec = periodNanoseconds;

		// Normalize period
		tsnorm(&thread->period);
	}


	// Get reference to thread function
	thread->javaThread = env->NewGlobalRef(target);
	jclass targetClass = env->GetObjectClass(target);

	JNIassert(env, targetClass != NULL);
	thread->methodID = env->GetMethodID(targetClass, "runThread", "()V");

	JNIassert(env, thread->methodID != NULL);

	return (long long)thread;
}

/**
 * Start thread
 *
 * @param threadPtr 64bit pointer to Thread
 */
JNIEXPORT jint JNICALL Java_us_ihmc_realtime_RealtimeNative_startThread(JNIEnv* env, jclass klass,
		jlong threadPtr)
{
	Thread* thread = (Thread*) threadPtr;

	pthread_attr_t attr;
	sched_param param;
	param.__sched_priority = thread->priority;

	JNIassert(env, pthread_attr_init(&attr) == 0);
	JNIassert(env, pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) == 0);
	JNIassert(env, pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) == 0);
	JNIassert(env, pthread_attr_setschedpolicy(&attr, SCHED_POLICY) == 0);
	JNIassert(env, pthread_attr_setschedparam(&attr, &param) == 0);

	int err = pthread_create(&thread->thread, &attr, run, thread);


	pthread_attr_destroy(&attr);

	return err;
}

/**
 * Wait till thread finishes
 */
JNIEXPORT jint JNICALL Java_us_ihmc_realtime_RealtimeNative_join
  (JNIEnv *env, jclass klass, jlong threadPtr)
{
	Thread* thread = (Thread*) threadPtr;
	int *retVal;
	pthread_join(thread->thread, (void**) &retVal);

	return *retVal;
}

long waitForAbsoluteTime(timespec* ts)
{
	timespec currentTime;
	clock_gettime(CLOCK_MONOTONIC, &currentTime);

	long delta = tsdelta(&currentTime, ts);

	if(delta < 0)
	{
		return delta;
	}

	while(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts, NULL) == EINTR);
	return delta;
}


/**
 * Wait for next period in thread
 *
 * @param threadPtr 64bit pointer to Thread
 */
JNIEXPORT jlong JNICALL Java_us_ihmc_realtime_RealtimeNative_waitForNextPeriod(JNIEnv* env, jclass klass,
		jlong threadPtr)
{
	Thread* thread = (Thread*) threadPtr;
	if(!thread->periodic)
	{
		throwRuntimeException(env, "Thread is not periodic");
	}

	tsadd(&thread->nextTrigger, &thread->period);

	return waitForAbsoluteTime(&thread->nextTrigger);
}

/**
 * Wait for a specific time
 *
 * @param threadPtr 64bit pointer to Thread
 * @param seconds Monotonic time, seconds part
 * @param nanoseconds Monotonic time, nanoseconds part
 */
JNIEXPORT jlong JNICALL Java_us_ihmc_realtime_RealtimeNative_waitUntil (JNIEnv* env , jclass klass, jlong threadPtr, jlong seconds, jlong nanoseconds)
{
	Thread* thread = (Thread*) threadPtr;
	timespec ts;
	ts.tv_sec = seconds;
	ts.tv_nsec = nanoseconds;
	tsnorm(&ts);

	return waitForAbsoluteTime(&ts);
}

/**
 * Set the next period to the current clock time
 *
 * @param threadPtr 64bit pointer to Thread
 */
JNIEXPORT void JNICALL Java_us_ihmc_realtime_RealtimeNative_setNextPeriodToClock
  (JNIEnv* env, jclass klass, jlong threadPtr)
{
	Thread* thread = (Thread*) threadPtr;
	if(!thread->periodic)
	{
		throwRuntimeException(env, "Thread is not periodic");
	}

	JNIassert(env, clock_gettime(CLOCK_MONOTONIC, &thread->nextTrigger) == 0);
	thread->setTriggerToClock = false;
}

JNIEXPORT void JNICALL Java_us_ihmc_realtime_RealtimeNative_setNextPeriod
  (JNIEnv* env, jclass klass, jlong threadPtr, jlong seconds, jlong nanoseconds)
{
	Thread* thread = (Thread*) threadPtr;
	if(!thread->periodic)
	{
		throwRuntimeException(env, "Thread is not periodic");
	}

	thread->nextTrigger.tv_sec = seconds;
	thread->nextTrigger.tv_nsec = nanoseconds;
	tsnorm(&thread->nextTrigger);
	thread->setTriggerToClock = false;
}

JNIEXPORT jlong JNICALL Java_us_ihmc_realtime_RealtimeNative_getNextPeriod
  (JNIEnv* env, jclass klass, jlong threadPtr)
{
	Thread* thread = (Thread*) threadPtr;
	if(!thread->periodic)
	{
		throwRuntimeException(env, "Thread is not periodic");
	}


	return (((long long) thread->nextTrigger.tv_sec) * NSEC_PER_SEC) + thread->nextTrigger.tv_nsec;
}

/**
 * Get maximum priority for a thread
 */
JNIEXPORT jint JNICALL Java_us_ihmc_realtime_RealtimeNative_getMaximumPriorityNative(JNIEnv* env,
		jclass klass)
{
	return sched_get_priority_max(SCHED_POLICY);
}

/**
 * Get minimum priority for a thread
 */
JNIEXPORT jint JNICALL Java_us_ihmc_realtime_RealtimeNative_getMinimumPriorityNative(JNIEnv* env,
		jclass klass)
{
	return sched_get_priority_min(SCHED_POLICY);
}

/**
 * Return current time from MONOTONIC clock
 */
JNIEXPORT jlong JNICALL Java_us_ihmc_realtime_RealtimeNative_getCurrentTimeNative(JNIEnv* env, jclass klass)
{
	struct timespec t;
	JNIassert(env, clock_gettime(CLOCK_MONOTONIC, &t) == 0);

	return (((long long) t.tv_sec) * NSEC_PER_SEC) + t.tv_nsec;
}

JNIEXPORT jint JNICALL Java_us_ihmc_realtime_RealtimeNative_getCurrentThreadPriority
  (JNIEnv *env, jclass klass)
{
	struct sched_param priority;
	int policy;

	JNIassert(env, pthread_getschedparam(pthread_self(), &policy, &priority) == 0);

	return priority.__sched_priority;

}

JNIEXPORT jint JNICALL Java_us_ihmc_realtime_RealtimeNative_getCurrentThreadScheduler
  (JNIEnv *env, jclass klass)
{
	struct sched_param priority;
	int policy;

	JNIassert(env, pthread_getschedparam(pthread_self(), &policy, &priority) == 0);

	return policy;
}
