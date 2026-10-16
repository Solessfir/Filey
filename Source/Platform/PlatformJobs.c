function b32 P_IsCancelled(const P_CancelToken *token)
{
    return token && atomic_load_explicit(&token->cancelled, memory_order_relaxed);
}

function void P_RequestCancel(P_CancelToken *token)
{
    if (token) {
        atomic_store_explicit(&token->cancelled, true, memory_order_relaxed);
    }
}

function void P__JobsRunWorker(P_JobQueue *queue)
{
    for (;;) {
        P__MutexLock(&queue->mutex);
        ProfileScope("WorkerIdle") {
            while (!queue->pendingFirst && !queue->stopping) {
                P__ConditionWait(&queue->condition, &queue->mutex);
            }
        }
        if (queue->stopping) {
            P__MutexUnlock(&queue->mutex);
            break;
        }
        P_Job *job = queue->pendingFirst;
        QueuePop(queue->pendingFirst, queue->pendingLast);
        job->next = NULL;
        job->state = P_JobState_Running;
        P__MutexUnlock(&queue->mutex);

        if (!P_IsCancelled(&job->cancelToken)) {
            ProfileScope("Job") job->callback(job);
        }

        P__MutexLock(&queue->mutex);
        job->state = P_JobState_Completed;
        QueuePush(queue->completedFirst, queue->completedLast, job);
        P__MutexUnlock(&queue->mutex);
        if (queue->wake) {
            queue->wake(queue->wakeUser);
        }
    }

    Scratch scratch = GetScratch();
    Scratch other = GetScratchEx(scratch.arena);
    ArenaRelease(scratch.arena);
    ArenaRelease(other.arena);
}

function b32 P_JobsInit(P_JobQueue *queue, Arena *permanent, u32 threadCount, P_JobWakeCallback *wake, void *wakeUser)
{
    AssertAlways(queue && permanent && !queue->initialized);
    ZeroStruct(queue);
    if (!P__MutexInit(&queue->mutex)) {
        return false;
    }
    if (!P__ConditionInit(&queue->condition)) {
        P__MutexRelease(&queue->mutex);
        return false;
    }
    queue->permanent = permanent;
    queue->wake = wake;
    queue->wakeUser = wakeUser;
    queue->initialized = true;
    if (threadCount == 0) {
        threadCount = Max((u32)2, Min(P__ProcessorCount(), (u32)4));
    }
    queue->threads = ArenaPushArray(permanent, threadCount, P_Thread);
    // Initialize the Windows timer's cached frequency before workers can query it.
    PlatformNowMicroseconds();
    for (u32 i = 0; i < threadCount; ++i) {
        if (!P__ThreadStart(&queue->threads[i], queue)) {
            P_JobsShutdown(queue);
            return false;
        }
        queue->threadCount += 1;
    }
    return true;
}

function void P_JobsShutdown(P_JobQueue *queue)
{
    if (!queue || !queue->initialized) {
        return;
    }
    P__MutexLock(&queue->mutex);
    queue->stopping = true;
    for (P_Job *job = queue->allFirst; job; job = job->allNext) {
        P_RequestCancel(&job->cancelToken);
    }
    P__ConditionBroadcast(&queue->condition);
    P__MutexUnlock(&queue->mutex);
    for (u32 i = 0; i < queue->threadCount; ++i) {
        P__ThreadJoin(&queue->threads[i]);
    }
    for (P_Job *job = queue->allFirst; job; job = job->allNext) {
        ArenaRelease(&job->arena);
    }
    P__ConditionRelease(&queue->condition);
    P__MutexRelease(&queue->mutex);
    ZeroStruct(queue);
}

function P_Job *P_JobCreate(P_JobQueue *queue, P_JobCallback *callback)
{
    AssertAlways(queue && queue->initialized && !queue->stopping && callback);
    P_Job *job = queue->freeFirst;
    if (job) {
        StackPop(queue->freeFirst);
    } else {
        job = ArenaPushStruct(queue->permanent, P_Job);
        StackPushN(queue->allFirst, job, allNext);
    }
    job->next = NULL;
    job->queue = queue;
    job->input = NULL;
    job->result = NULL;
    job->callback = callback;
    job->state = P_JobState_Owned;
    atomic_init(&job->cancelToken.cancelled, false);
    return job;
}

function void P_JobSubmit(P_Job *job)
{
    AssertAlways(job && job->state == P_JobState_Owned);
    P_JobQueue *queue = job->queue;
    AssertAlways(queue->initialized && !queue->stopping);
    P__MutexLock(&queue->mutex);
    job->state = P_JobState_Pending;
    QueuePush(queue->pendingFirst, queue->pendingLast, job);
    P__ConditionSignal(&queue->condition);
    P__MutexUnlock(&queue->mutex);
}

function P_Job *P_JobTakeCompleted(P_JobQueue *queue)
{
    AssertAlways(queue && queue->initialized);
    P__MutexLock(&queue->mutex);
    P_Job *job = queue->completedFirst;
    if (job) {
        QueuePop(queue->completedFirst, queue->completedLast);
        job->next = NULL;
        job->state = P_JobState_Owned;
    }
    P__MutexUnlock(&queue->mutex);
    return job;
}

function void P_JobRelease(P_Job *job)
{
    if (!job) {
        return;
    }
    AssertAlways(job->state == P_JobState_Owned && job->queue->initialized);
    ArenaRelease(&job->arena);
    job->input = NULL;
    job->result = NULL;
    job->callback = NULL;
    job->state = P_JobState_Free;
    StackPush(job->queue->freeFirst, job);
}

typedef struct P__Lane {
    P_LaneCallback *callback;
    void *user;
    u32 index;
} P__Lane;

// One P_RunLanes call: how many of its lanes are still running on kept threads.
typedef struct P__LaneCall {
    P_Mutex mutex;
    P_Condition done;
    u32 running;
} P__LaneCall;

// Lanes run on threads kept for them: a call borrows idle ones and starts more only while every kept one is busy, so that lanes run every
// frame start no threads; each new thread costs memory that some drivers never give back. Calls from several threads at once get their own.
typedef struct P__LaneWorker {
    P_Thread thread;
    P_Mutex mutex;
    P_Condition wake;
    P__Lane *lane;               // the lane to run, NULL while idle
    P__LaneCall *call;
    struct P__LaneWorker *nextFree;
} P__LaneWorker;

// ponytail: a fixed number of kept lane threads; past them a lane runs on its caller's thread.
#define P__MaxLaneWorkers 128
global P__LaneWorker P__laneWorkers[P__MaxLaneWorkers];
global u32 P__laneWorkerCount;
global P__LaneWorker *P__laneFree;
global atomic_flag P__laneLock = ATOMIC_FLAG_INIT;

function void P__LaneLock(void)
{
    while (atomic_flag_test_and_set_explicit(&P__laneLock, memory_order_acquire)) {}
}

function void P__LaneUnlock(void)
{
    atomic_flag_clear_explicit(&P__laneLock, memory_order_release);
}

function void P__LaneMain(void *parameter)
{
    P__LaneWorker *worker = parameter;
    for (;;) {
        P__MutexLock(&worker->mutex);
        while (!worker->lane) P__ConditionWait(&worker->wake, &worker->mutex);
        P__Lane *lane = worker->lane;
        P__LaneCall *call = worker->call;
        worker->lane = NULL;
        P__MutexUnlock(&worker->mutex);
        lane->callback(lane->user, lane->index);
        ReleaseThreadScratch();
        // Idle again before its caller hears it is done, so that the caller's next call can take it again.
        P__LaneLock();
        worker->nextFree = P__laneFree;
        P__laneFree = worker;
        P__LaneUnlock();
        P__MutexLock(&call->mutex);
        --call->running;
        P__ConditionSignal(&call->done);
        P__MutexUnlock(&call->mutex);
    }
}

// An idle kept thread, or a new one while there is room; NULL when neither is to be had.
function P__LaneWorker *P__TakeLaneWorker(void)
{
    P__LaneLock();
    P__LaneWorker *worker = P__laneFree;
    if (worker) P__laneFree = worker->nextFree;
    u32 at = !worker && P__laneWorkerCount < P__MaxLaneWorkers ? P__laneWorkerCount++ : UINT32_MAX;
    P__LaneUnlock();
    if (worker || at == UINT32_MAX) return worker;
    worker = &P__laneWorkers[at];
    if (!P__MutexInit(&worker->mutex)) return NULL;
    if (!P__ConditionInit(&worker->wake)) {
        P__MutexRelease(&worker->mutex);
        return NULL;
    }
    if (!P__ThreadStartLane(&worker->thread, worker)) {
        P__ConditionRelease(&worker->wake);
        P__MutexRelease(&worker->mutex);
        return NULL;
    }
    return worker;
}

function void P_RunLanes(u32 count, P_LaneCallback *callback, void *user)
{
    count = Max(1u, Min(count, (u32)P_MaxLanes));
    P__Lane lanes[P_MaxLanes];
    for (u32 i = 0; i < count; ++i) lanes[i] = (P__Lane){ callback, user, i };
    P__LaneCall call = {0};
    b32 ready = count > 1 && P__MutexInit(&call.mutex);
    if (ready && !P__ConditionInit(&call.done)) {
        P__MutexRelease(&call.mutex);
        ready = false;
    }
    b32 handed[P_MaxLanes] = {0};
    for (u32 i = 1; ready && i < count; ++i) {
        P__LaneWorker *worker = P__TakeLaneWorker();
        if (!worker) continue;
        handed[i] = true;
        P__MutexLock(&call.mutex);
        ++call.running;
        P__MutexUnlock(&call.mutex);
        P__MutexLock(&worker->mutex);
        worker->lane = &lanes[i];
        worker->call = &call;
        P__ConditionSignal(&worker->wake);
        P__MutexUnlock(&worker->mutex);
    }
    callback(user, 0);
    // A lane without a thread runs here instead, once the others have the work.
    for (u32 i = 1; i < count; ++i) {
        if (!handed[i]) callback(user, i);
    }
    if (ready) {
        P__MutexLock(&call.mutex);
        while (call.running) P__ConditionWait(&call.done, &call.mutex);
        P__MutexUnlock(&call.mutex);
        P__ConditionRelease(&call.done);
        P__MutexRelease(&call.mutex);
    }
}

function b32 P_WaitGone(const String *paths, u64 count, u32 milliseconds)
{
    for (u32 waited = 0;; waited += 50) {
        b32 left = false;
        for (u64 i = 0; i < count && !left; ++i) left = P_PathExists(paths[i]);
        if (!left) return true;
        if (waited >= milliseconds) return false;
        P_Sleep(50);
    }
}

function u32 P_ProcessorCount(void) { return P__ProcessorCount(); }
function b32 P_MutexInit(P_Mutex *mutex) { return P__MutexInit(mutex); }
function void P_MutexRelease(P_Mutex *mutex) { P__MutexRelease(mutex); }
function void P_MutexLock(P_Mutex *mutex) { P__MutexLock(mutex); }
function void P_MutexUnlock(P_Mutex *mutex) { P__MutexUnlock(mutex); }
function b32 P_ConditionInit(P_Condition *condition) { return P__ConditionInit(condition); }
function void P_ConditionRelease(P_Condition *condition) { P__ConditionRelease(condition); }
function void P_ConditionWait(P_Condition *condition, P_Mutex *mutex) { P__ConditionWait(condition, mutex); }
function void P_ConditionBroadcast(P_Condition *condition) { P__ConditionBroadcast(condition); }
