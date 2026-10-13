// Event buffering shared by the OS window implementations.
//
// OS callbacks can fire both inside and between PlatformPollEvents calls, so events always go to a pending list.
// Two arenas alternate: the list returned by one poll stays valid until the next poll clears its arena.

typedef struct PlatformEventQueue {
    Arena arenas[2];
    u32 current;
    PlatformEventList pending;
} PlatformEventQueue;

global PlatformEventQueue EventQueue;
global PlatformFrameCallback *ModalFrameCallback;
global void *ModalFrameUser;
global b32 ModalFrameActive;

function PlatformEvent *PlatformPushEvent(PlatformEventKind kind)
{
    PlatformEvent *event = ArenaPushStruct(&EventQueue.arenas[EventQueue.current], PlatformEvent);
    event->kind = (u8)kind;
    event->timeMicroseconds = PlatformNowMicroseconds();
    QueuePush(EventQueue.pending.first, EventQueue.pending.last, event);
    EventQueue.pending.count += 1;
    return event;
}

function PlatformEventList PlatformTakeEvents(void)
{
    PlatformEventList result = EventQueue.pending;

    EventQueue.current ^= 1;
    ArenaClear(&EventQueue.arenas[EventQueue.current]);
    EventQueue.pending = (PlatformEventList){0};

    return result;
}

function void PlatformSetModalFrameCallback(PlatformFrameCallback *callback, void *user)
{
    ModalFrameCallback = callback;
    ModalFrameUser = user;
}

function void Platform__RunModalFrame(void)
{
    if (ModalFrameCallback && !ModalFrameActive) {
        ModalFrameActive = true;
        ModalFrameCallback(ModalFrameUser, PlatformTakeEvents());
        ModalFrameActive = false;
    }
}
