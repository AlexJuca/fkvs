#ifndef FKVS_TIME_H
#define FKVS_TIME_H

#include <stdint.h>
#include <time.h>

static inline int64_t fkvs_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif // FKVS_TIME_H
