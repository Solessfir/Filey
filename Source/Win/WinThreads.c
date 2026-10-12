// Requires WinPlatform.c (windows.h).

static_assert(sizeof(SRWLOCK) <= sizeof(((P_Mutex *)0)->storage));
static_assert(alignof(SRWLOCK) <= alignof(P_Mutex));
static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(((P_Condition *)0)->storage));
static_assert(alignof(CONDITION_VARIABLE) <= alignof(P_Condition));
static_assert(sizeof(HANDLE) <= sizeof(((P_Thread *)0)->storage));
static_assert(alignof(HANDLE) <= alignof(P_Thread));

function b32 P__MutexInit(P_Mutex *mutex)
{
    InitializeSRWLock((SRWLOCK *)mutex->storage);
    return true;
}

function void P__MutexRelease(P_Mutex *mutex)
{
    (void)mutex;
}

function void P__MutexLock(P_Mutex *mutex)
{
    AcquireSRWLockExclusive((SRWLOCK *)mutex->storage);
}

function void P__MutexUnlock(P_Mutex *mutex)
{
    ReleaseSRWLockExclusive((SRWLOCK *)mutex->storage);
}

function b32 P__ConditionInit(P_Condition *condition)
{
    InitializeConditionVariable((CONDITION_VARIABLE *)condition->storage);
    return true;
}

function void P__ConditionRelease(P_Condition *condition)
{
    (void)condition;
}

function void P__ConditionWait(P_Condition *condition, P_Mutex *mutex)
{
    AssertAlways(SleepConditionVariableSRW((CONDITION_VARIABLE *)condition->storage, (SRWLOCK *)mutex->storage, INFINITE, 0));
}

function void P__ConditionSignal(P_Condition *condition)
{
    WakeConditionVariable((CONDITION_VARIABLE *)condition->storage);
}

function void P__ConditionBroadcast(P_Condition *condition)
{
    WakeAllConditionVariable((CONDITION_VARIABLE *)condition->storage);
}

function DWORD WINAPI Win__JobWorker(void *user)
{
    P__JobsRunWorker(user);
    return 0;
}

function b32 P__ThreadStart(P_Thread *thread, P_JobQueue *queue)
{
    HANDLE handle = CreateThread(NULL, 0, Win__JobWorker, queue, 0, NULL);
    MemCopy(thread->storage, &handle, sizeof(handle));
    return handle != NULL;
}

function void P__ThreadJoin(P_Thread *thread)
{
    HANDLE handle;
    MemCopy(&handle, thread->storage, sizeof(handle));
    AssertAlways(WaitForSingleObject(handle, INFINITE) == WAIT_OBJECT_0);
    AssertAlways(CloseHandle(handle));
}

function u32 P__ProcessorCount(void)
{
    SYSTEM_INFO info = {0};
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}
