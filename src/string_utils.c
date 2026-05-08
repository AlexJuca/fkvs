#include "string_utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *to_upper(const char *string)
{
    const size_t len = strlen(string);
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    for (size_t i = 0; i < len; ++i) {
        result[i] = (char)toupper((unsigned char)string[i]);
    }
    result[len] = '\0';
    return result;
}
