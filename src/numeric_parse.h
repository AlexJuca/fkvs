#ifndef NUMERIC_PARSE_H
#define NUMERIC_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool fkvs_parse_i64_decimal(const unsigned char *buf, size_t len,
                            int64_t min_value, int64_t max_value,
                            int64_t *out);

bool fkvs_parse_deadline_ms(const unsigned char *buf, size_t len,
                            int64_t now_ms, int64_t *deadline_ms);

#endif // NUMERIC_PARSE_H
