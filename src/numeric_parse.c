#include "numeric_parse.h"

#include <limits.h>

static uint64_t signed_abs_limit(int64_t value)
{
    if (value == INT64_MIN)
        return (uint64_t)INT64_MAX + 1U;
    if (value < 0)
        return (uint64_t)(-value);
    return (uint64_t)value;
}

bool fkvs_parse_i64_decimal(const unsigned char *buf, size_t len,
                            int64_t min_value, int64_t max_value,
                            int64_t *out)
{
    if (!buf || !out || len == 0 || min_value > max_value)
        return false;

    size_t pos = 0;
    bool negative = false;
    if (buf[pos] == '+' || buf[pos] == '-') {
        negative = buf[pos] == '-';
        pos++;
        if (pos == len)
            return false;
    }

    if (negative && min_value >= 0)
        return false;
    if (!negative && max_value < 0)
        return false;

    const uint64_t limit =
        negative ? signed_abs_limit(min_value) : (uint64_t)max_value;
    uint64_t value = 0;

    for (; pos < len; pos++) {
        const unsigned char ch = buf[pos];
        if (ch < '0' || ch > '9')
            return false;

        const uint64_t digit = (uint64_t)(ch - '0');
        if (value > (limit - digit) / 10)
            return false;
        value = value * 10 + digit;
    }

    if (negative) {
        if (value == (uint64_t)INT64_MAX + 1U)
            *out = INT64_MIN;
        else
            *out = -(int64_t)value;
    } else {
        *out = (int64_t)value;
    }

    return *out >= min_value && *out <= max_value;
}

bool fkvs_parse_deadline_ms(const unsigned char *buf, size_t len,
                            int64_t now_ms, int64_t *deadline_ms)
{
    int64_t seconds;
    if (!deadline_ms ||
        !fkvs_parse_i64_decimal(buf, len, 0, INT64_MAX / 1000, &seconds)) {
        return false;
    }

    if (seconds > (INT64_MAX - now_ms) / 1000)
        return false;

    *deadline_ms = now_ms + seconds * 1000;
    return true;
}
