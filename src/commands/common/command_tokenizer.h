#ifndef COMMAND_TOKENIZER_H
#define COMMAND_TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Quote-aware tokenizer for CLI command lines.
 *
 * The CLI receives a raw text line (e.g. `SET user '{"a": 1}'`) and must split
 * it into discrete arguments. A plain whitespace split breaks any value that
 * contains spaces, so this module treats single- or double-quoted spans as a
 * single token, stripping the surrounding quotes. This keeps values such as
 * '{"name":"alex", "age": "12"}' intact instead of being shredded on spaces.
 *
 * Use command_next_token() to pull arguments one at a time from a cursor, or
 * command_tokenize() to split an entire line into an argv-style array in one
 * call.
 */

#define CMD_MAX_TOKENS 8
#define CMD_MAX_TOKEN_LEN 512

typedef struct {
    int argc;
    char argv[CMD_MAX_TOKENS][CMD_MAX_TOKEN_LEN];
} command_tokens_t;

/*
 * Read the next token from *cursor into out (NUL-terminated, never overflowing
 * out_size). A token starting with ' or " runs to the matching closing quote
 * (embedded spaces and the other quote character are preserved); otherwise it
 * runs to the next whitespace. Advances *cursor past the token.
 *
 * Returns true if a token was produced, false when only trailing whitespace
 * (or end of string) remains.
 */
bool command_next_token(const char **cursor, char *out, size_t out_size);

/*
 * Split input into quote-aware tokens, filling tokens->argv / tokens->argc.
 * At most CMD_MAX_TOKENS are produced; any beyond that are discarded and the
 * call reports the overflow.
 *
 * Returns the number of tokens parsed (== tokens->argc). If the input held
 * more than CMD_MAX_TOKENS tokens, returns -1 (tokens->argc still reflects the
 * tokens that fit) so callers can reject over-long command lines.
 */
int command_tokenize(const char *input, command_tokens_t *tokens);

#endif // COMMAND_TOKENIZER_H
