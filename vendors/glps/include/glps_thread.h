#ifndef GLPS_THREAD_H
#define GLPS_THREAD_H

/**
 * @file glps_thread.h
 * @brief Cross-platform thread, mutex, and condition variable abstractions for GLPS.
 *
 * Provides a unified interface for threads, mutexes, and condition variables on
 * Windows and POSIX platforms.
 */

// Platform detection
#if defined(GLPS_USE_WIN32)
    #define GLPS_THREAD_WIN32
#elif defined(GLPS_USE_WAYLAND) || defined(GLPS_USE_X11)
    #define GLPS_THREAD_POSIX
#endif

#include <stdint.h>  /**< For uintptr_t */
#include <errno.h>   /**< For standard error codes like EBUSY */

#ifdef WIN32
    #include <windows.h>

    typedef HANDLE gthread_t;              /**< Thread handle type for Windows */
    typedef DWORD gthread_attr_t;          /**< Thread attribute type for Windows */

    typedef CRITICAL_SECTION gthread_mutex_t; /**< Mutex type for Windows */
    typedef int gthread_mutexattr_t;           /**< Mutex attribute type (unused) */

    typedef CONDITION_VARIABLE gthread_cond_t; /**< Condition variable type for Windows */
    typedef int gthread_condattr_t;            /**< Condition variable attribute type (unused) */

#else // POSIX
    #include <pthread.h>

    typedef pthread_t gthread_t;              /**< Thread handle type for POSIX */
    typedef pthread_attr_t gthread_attr_t;    /**< Thread attribute type for POSIX */

    typedef pthread_mutex_t gthread_mutex_t;       /**< Mutex type for POSIX */
    typedef pthread_mutexattr_t gthread_mutexattr_t; /**< Mutex attribute type for POSIX */

    typedef pthread_cond_t gthread_cond_t;       /**< Condition variable type for POSIX */
    typedef pthread_condattr_t gthread_condattr_t; /**< Condition variable attribute type for POSIX */

#endif

/** @name Thread Functions
 * Functions for creating and managing threads.
 * @{
 */

/**
 * @brief Creates a new thread.
 *
 * Starts a new thread that executes the given start routine.
 *
 * @param thread Pointer to store the created thread handle.
 * @param attr Optional thread attributes (can be NULL).
 * @param start_routine Function pointer for the thread's main routine.
 * @param arg Argument to pass to start_routine.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_create(gthread_t *thread, const gthread_attr_t *attr,
                       void *(*start_routine)(void *), void *arg);

/**
 * @brief Waits for a thread to terminate.
 *
 * Blocks the calling thread until the specified thread finishes execution.
 *
 * @param thread Thread handle to wait for.
 * @param retval Optional pointer to store the thread's return value (can be NULL).
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_join(gthread_t thread, void **retval);

/**
 * @brief Detaches a thread.
 *
 * Marks the thread as detached so its resources are automatically released upon termination.
 *
 * @param thread Thread handle to detach.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_detach(gthread_t thread);

/**
 * @brief Exits the calling thread.
 *
 * Terminates the current thread and optionally provides a return value.
 *
 * @param retval Pointer to the return value of the thread.
 */
void glps_thread_exit(void *retval);

/**
 * @brief Returns the calling thread's handle.
 *
 * @return Thread handle representing the calling thread.
 */
gthread_t glps_thread_self(void);

/**
 * @brief Compares two threads for equality.
 *
 * @param t1 First thread handle.
 * @param t2 Second thread handle.
 * @return Non-zero if threads are equal, 0 otherwise.
 */
int glps_thread_equal(gthread_t t1, gthread_t t2);

/** @} */

/** @name Mutex Functions
 * Functions for creating and managing mutexes.
 * @{
 */

/**
 * @brief Initializes a mutex.
 *
 * @param mutex Pointer to the mutex object.
 * @param attr Optional mutex attributes (can be NULL).
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_mutex_init(gthread_mutex_t *mutex, const gthread_mutexattr_t *attr);

/**
 * @brief Destroys a mutex.
 *
 * @param mutex Pointer to the mutex to destroy.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_mutex_destroy(gthread_mutex_t *mutex);

/**
 * @brief Locks a mutex.
 *
 * Blocks the calling thread until the mutex is acquired.
 *
 * @param mutex Pointer to the mutex to lock.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_mutex_lock(gthread_mutex_t *mutex);

/**
 * @brief Unlocks a mutex.
 *
 * Releases a previously acquired mutex.
 *
 * @param mutex Pointer to the mutex to unlock.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_mutex_unlock(gthread_mutex_t *mutex);

/**
 * @brief Attempts to lock a mutex without blocking.
 *
 * @param mutex Pointer to the mutex.
 * @return 0 if the mutex was successfully locked, EBUSY if already locked, or other error code.
 */
int glps_thread_mutex_trylock(gthread_mutex_t *mutex);

/** @} */

/** @name Condition Variable Functions
 * Functions for creating and managing condition variables.
 * @{
 */

/**
 * @brief Initializes a condition variable.
 *
 * @param cond Pointer to the condition variable.
 * @param attr Optional condition variable attributes (can be NULL).
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_cond_init(gthread_cond_t *cond, const gthread_condattr_t *attr);

/**
 * @brief Destroys a condition variable.
 *
 * @param cond Pointer to the condition variable to destroy.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_cond_destroy(gthread_cond_t *cond);

/**
 * @brief Waits on a condition variable.
 *
 * Unlocks the given mutex and blocks until the condition variable is signaled.
 * The mutex is automatically re-acquired before returning.
 *
 * @param cond Pointer to the condition variable.
 * @param mutex Pointer to the associated mutex.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_cond_wait(gthread_cond_t *cond, gthread_mutex_t *mutex);

/**
 * @brief Signals a condition variable.
 *
 * Wakes up one thread waiting on the condition variable.
 *
 * @param cond Pointer to the condition variable.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_cond_signal(gthread_cond_t *cond);

/**
 * @brief Broadcasts a condition variable.
 *
 * Wakes up all threads waiting on the condition variable.
 *
 * @param cond Pointer to the condition variable.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_cond_broadcast(gthread_cond_t *cond);

/** @} */

/** @name Thread Attribute Functions
 * Functions for creating and managing thread attributes.
 * @{
 */

/**
 * @brief Initializes a thread attribute object.
 *
 * @param attr Pointer to the attribute object.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_attr_init(gthread_attr_t *attr);

/**
 * @brief Destroys a thread attribute object.
 *
 * @param attr Pointer to the attribute object.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_attr_destroy(gthread_attr_t *attr);

/**
 * @brief Sets the detach state of a thread attribute.
 *
 * @param attr Pointer to the attribute object.
 * @param detachstate Detach state (0 = joinable, 1 = detached).
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_attr_setdetachstate(gthread_attr_t *attr, int detachstate);

/**
 * @brief Gets the detach state of a thread attribute.
 *
 * @param attr Pointer to the attribute object.
 * @param detachstate Pointer to store the detach state.
 * @return 0 on success, non-zero error code on failure.
 */
int glps_thread_attr_getdetachstate(const gthread_attr_t *attr, int *detachstate);

/** @} */

#endif // GLPS_THREAD_H
