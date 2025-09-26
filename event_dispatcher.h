#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#define MAX_EVENTS 100000

// Defines the interface for platform-specific event loops
int run_event_loop();

#endif // EVENT_DISPATCHER_H
