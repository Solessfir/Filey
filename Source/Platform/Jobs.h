#pragma once

#include <stdatomic.h>

typedef struct P_CancelToken {
    atomic_bool cancelled;
} P_CancelToken;

typedef struct P_Mutex {
    alignas(max_align_t) u8 storage[64];
} P_Mutex;

typedef struct P_Condition {
    alignas(max_align_t) u8 storage[64];
} P_Condition;

typedef struct P_Thread {
    alignas(max_align_t) u8 storage[16];
} P_Thread;

typedef enum P_JobState {
    P_JobState_Free,
    P_JobState_Owned,
    P_JobState_Pending,
    P_JobState_Running,
    P_JobState_Completed,
    P_JobState_Count,
} P_JobState;

typedef struct P_Job P_Job;
typedef struct P_JobQueue P_JobQueue;
typedef void P_JobCallback(P_Job *job);
typedef void P_JobWakeCallback(void *user);

struct P_Job {
    P_Job *next;
    P_Job *allNext;
    P_JobQueue *queue;
    Arena arena;
    void *input;
    void *result;
    P_JobCallback *callback;
    P_CancelToken cancelToken;
    enum8(P_JobState) state;
};

struct P_JobQueue {
    Arena *permanent;
    P_Mutex mutex;
    P_Condition condition;
    P_Thread *threads;
    u32 threadCount;
    b32 initialized;
    b32 stopping;
    P_Job *pendingFirst;
    P_Job *pendingLast;
    P_Job *completedFirst;
    P_Job *completedLast;
    P_Job *allFirst;
    P_Job *freeFirst;
    P_JobWakeCallback *wake;
    void *wakeUser;
};

function b32 P_IsCancelled(const P_CancelToken *token);
function void P_RequestCancel(P_CancelToken *token);

// Queue lifecycle and job ownership operations run on the main thread. Zero threads selects 2-4 workers.
function b32 P_JobsInit(P_JobQueue *queue, Arena *permanent, u32 threadCount, P_JobWakeCallback *wake, void *wakeUser);
// Cancels outstanding work, joins workers, and releases every job arena before returning.
function void P_JobsShutdown(P_JobQueue *queue);
// Fill copied input in the returned job's arena before submitting it.
function P_Job *P_JobCreate(P_JobQueue *queue, P_JobCallback *callback);
// Submission transfers the job arena to the worker. Only cancellation and immutable input reads remain available until completion.
function void P_JobSubmit(P_Job *job);
// Completion transfers the job and arena back to the caller, which must eventually release it.
function P_Job *P_JobTakeCompleted(P_JobQueue *queue);
function void P_JobRelease(P_Job *job);

// Thread-safe native event-loop wakeup. The window must outlive the workers.
function void P_WakeWindow(void);
// Release the wakeup resource after all workers have joined.
function void P_ReleaseWindowWake(void);

function b32 P__MutexInit(P_Mutex *mutex);
function void P__MutexRelease(P_Mutex *mutex);
function void P__MutexLock(P_Mutex *mutex);
function void P__MutexUnlock(P_Mutex *mutex);
function b32 P__ConditionInit(P_Condition *condition);
function void P__ConditionRelease(P_Condition *condition);
function void P__ConditionWait(P_Condition *condition, P_Mutex *mutex);
function void P__ConditionSignal(P_Condition *condition);
function void P__ConditionBroadcast(P_Condition *condition);
function b32 P__ThreadStart(P_Thread *thread, P_JobQueue *queue);
function void P__ThreadJoin(P_Thread *thread);
function u32 P__ProcessorCount(void);
function void P__JobsRunWorker(P_JobQueue *queue);
