#include <pthread.h>

static_assert(sizeof(pthread_mutex_t) <= sizeof(((P_Mutex *)0)->storage));
static_assert(alignof(pthread_mutex_t) <= alignof(P_Mutex));
static_assert(sizeof(pthread_cond_t) <= sizeof(((P_Condition *)0)->storage));
static_assert(alignof(pthread_cond_t) <= alignof(P_Condition));
static_assert(sizeof(pthread_t) <= sizeof(((P_Thread *)0)->storage));
static_assert(alignof(pthread_t) <= alignof(P_Thread));

function b32 P__MutexInit(P_Mutex *mutex)
{
    return pthread_mutex_init((pthread_mutex_t *)mutex->storage, NULL) == 0;
}

function void P__MutexRelease(P_Mutex *mutex)
{
    AssertAlways(pthread_mutex_destroy((pthread_mutex_t *)mutex->storage) == 0);
}

function void P__MutexLock(P_Mutex *mutex)
{
    AssertAlways(pthread_mutex_lock((pthread_mutex_t *)mutex->storage) == 0);
}

function void P__MutexUnlock(P_Mutex *mutex)
{
    AssertAlways(pthread_mutex_unlock((pthread_mutex_t *)mutex->storage) == 0);
}

function b32 P__ConditionInit(P_Condition *condition)
{
    return pthread_cond_init((pthread_cond_t *)condition->storage, NULL) == 0;
}

function void P__ConditionRelease(P_Condition *condition)
{
    AssertAlways(pthread_cond_destroy((pthread_cond_t *)condition->storage) == 0);
}

function void P__ConditionWait(P_Condition *condition, P_Mutex *mutex)
{
    AssertAlways(pthread_cond_wait((pthread_cond_t *)condition->storage, (pthread_mutex_t *)mutex->storage) == 0);
}

function void P__ConditionSignal(P_Condition *condition)
{
    AssertAlways(pthread_cond_signal((pthread_cond_t *)condition->storage) == 0);
}

function void P__ConditionBroadcast(P_Condition *condition)
{
    AssertAlways(pthread_cond_broadcast((pthread_cond_t *)condition->storage) == 0);
}

function void *Linux__JobWorker(void *user)
{
    P__JobsRunWorker(user);
    return NULL;
}

function b32 P__ThreadStart(P_Thread *thread, P_JobQueue *queue)
{
    pthread_t handle;
    if (pthread_create(&handle, NULL, Linux__JobWorker, queue) != 0) {
        return false;
    }
    MemCopy(thread->storage, &handle, sizeof(handle));
    return true;
}

function void P__ThreadJoin(P_Thread *thread)
{
    pthread_t handle;
    MemCopy(&handle, thread->storage, sizeof(handle));
    AssertAlways(pthread_join(handle, NULL) == 0);
}

function u32 P__ProcessorCount(void)
{
    i64 count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (u32)count : 1;
}
