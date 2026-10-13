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
