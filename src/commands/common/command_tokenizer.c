#include "../common/command_tokenizer.h"

#include <ctype.h>
#include <string.h>

bool command_next_token(const char **cursor, char *out, size_t out_size)
{
    const char *s = *cursor;
    while (isspace((unsigned char)*s))
        s++;

    if (*s == '\0') {
        *cursor = s;
        return false;
    }

    size_t len = 0;
    if (*s == '\'' || *s == '"') {
        const char quote = *s++;
        while (*s != '\0' && *s != quote) {
            if (len + 1 < out_size)
                out[len++] = *s;
            s++;
        }
        if (*s == quote)
            s++; /* consume closing quote */
    } else {
        while (*s != '\0' && !isspace((unsigned char)*s)) {
            if (len + 1 < out_size)
                out[len++] = *s;
            s++;
        }
    }

    out[len] = '\0';
    *cursor = s;
    return true;
}

int command_tokenize(const char *input, command_tokens_t *tokens)
{
    const char *cursor = input;
    tokens->argc = 0;

    char scratch[CMD_MAX_TOKEN_LEN];
    while (command_next_token(&cursor, scratch, sizeof(scratch))) {
        if (tokens->argc >= CMD_MAX_TOKENS) {
            return -1; /* more tokens than we can hold */
        }
        memcpy(tokens->argv[tokens->argc], scratch, sizeof(scratch));
        tokens->argc++;
    }

    return tokens->argc;
}
