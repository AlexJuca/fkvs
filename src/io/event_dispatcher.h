#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#define MAX_EVENTS 100000

typedef enum event_loop_dispatcher_kind {
    kqueue_kind,
    epoll_kind,
    io_uring_kind
} event_loop_dispatcher_kind;

static char *
event_loop_dispatcher_kind_to_string(const event_loop_dispatcher_kind kind)
{
    switch (kind) {
    case kqueue_kind:
        return "kqueue";
    case epoll_kind:
        return "epoll";
    case io_uring_kind:
        return "io_uring";
    default:
        return "(unknown)";
    }
}

// Defines the interface for platform-specific event loops
int run_event_loop();

#endif // EVENT_DISPATCHER_H
