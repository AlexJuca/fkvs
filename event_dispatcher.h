#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#define MAX_EVENTS 4096

// Defines the interface for platform-specific event loops
int run_event_loop(int server_fd);

#endif // EVENT_DISPATCHER_H
