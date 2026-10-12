// Event buffering shared by the OS window implementations.
//
// OS callbacks can fire both inside and between PlatformPollEvents calls, so
// events always go to a pending list. Two arenas alternate: the list returned
// by one poll stays valid until the next poll clears its arena.

typedef struct PlatformEventQueue {
    Arena arenas[2];
    u32 current;
    PlatformEventList pending;
} PlatformEventQueue;

global PlatformEventQueue EventQueue;

function PlatformEvent *PlatformPushEvent(PlatformEventKind kind)
{
    PlatformEvent *event = ArenaPushStruct(&EventQueue.arenas[EventQueue.current], PlatformEvent);
    event->kind = (u8)kind;
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
