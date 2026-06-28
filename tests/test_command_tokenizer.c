#include "../src/commands/common/command_tokenizer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_plain_split(void)
{
    command_tokens_t t;
    int rc = command_tokenize("SET k1 hello", &t);
    assert(rc == 3 && t.argc == 3);
    assert(strcmp(t.argv[0], "SET") == 0);
    assert(strcmp(t.argv[1], "k1") == 0);
    assert(strcmp(t.argv[2], "hello") == 0);
    printf("test_plain_split passed.\n");
}

static void test_single_quoted_value_with_spaces(void)
{
    /* The bug that motivated the tokenizer: a JSON value full of spaces and
     * double quotes must survive as a single argument. */
    command_tokens_t t;
    int rc = command_tokenize("SET user '{\"name\":\"alex\", \"age\": \"12\"}'", &t);
    assert(rc == 3 && t.argc == 3);
    assert(strcmp(t.argv[1], "user") == 0);
    assert(strcmp(t.argv[2], "{\"name\":\"alex\", \"age\": \"12\"}") == 0);
    printf("test_single_quoted_value_with_spaces passed.\n");
}

static void test_double_quoted_value_with_spaces(void)
{
    command_tokens_t t;
    int rc = command_tokenize("SET key \"a quoted value\" EX 5", &t);
    assert(rc == 5 && t.argc == 5);
    assert(strcmp(t.argv[2], "a quoted value") == 0);
    assert(strcmp(t.argv[3], "EX") == 0);
    assert(strcmp(t.argv[4], "5") == 0);
    printf("test_double_quoted_value_with_spaces passed.\n");
}

static void test_leading_and_extra_whitespace_collapsed(void)
{
    command_tokens_t t;
    int rc = command_tokenize("   GET    user   ", &t);
    assert(rc == 2 && t.argc == 2);
    assert(strcmp(t.argv[0], "GET") == 0);
    assert(strcmp(t.argv[1], "user") == 0);
    printf("test_leading_and_extra_whitespace_collapsed passed.\n");
}

static void test_empty_input_yields_no_tokens(void)
{
    command_tokens_t t;
    int rc = command_tokenize("    ", &t);
    assert(rc == 0 && t.argc == 0);
    printf("test_empty_input_yields_no_tokens passed.\n");
}

static void test_unterminated_quote_runs_to_end(void)
{
    /* A missing closing quote captures the remainder of the line. */
    command_tokens_t t;
    int rc = command_tokenize("SET k 'no closing quote here", &t);
    assert(rc == 3 && t.argc == 3);
    assert(strcmp(t.argv[2], "no closing quote here") == 0);
    printf("test_unterminated_quote_runs_to_end passed.\n");
}

static void test_overflow_reports_minus_one(void)
{
    /* More than CMD_MAX_TOKENS tokens: argc saturates, rc signals overflow. */
    command_tokens_t t;
    int rc = command_tokenize("a b c d e f g h i j", &t);
    assert(rc == -1);
    assert(t.argc == CMD_MAX_TOKENS);
    printf("test_overflow_reports_minus_one passed.\n");
}

static void test_token_length_is_bounded(void)
{
    /* A token longer than CMD_MAX_TOKEN_LEN is truncated, never overflowed. */
    char input[CMD_MAX_TOKEN_LEN * 2];
    memset(input, 'x', sizeof(input));
    input[sizeof(input) - 1] = '\0';

    command_tokens_t t;
    int rc = command_tokenize(input, &t);
    assert(rc == 1 && t.argc == 1);
    assert(strlen(t.argv[0]) == CMD_MAX_TOKEN_LEN - 1);
    printf("test_token_length_is_bounded passed.\n");
}

int main(void)
{
    test_plain_split();
    test_single_quoted_value_with_spaces();
    test_double_quoted_value_with_spaces();
    test_leading_and_extra_whitespace_collapsed();
    test_empty_input_yields_no_tokens();
    test_unterminated_quote_runs_to_end();
    test_overflow_reports_minus_one();
    test_token_length_is_bounded();
    printf("All command_tokenizer tests passed.\n");
    return 0;
}
