#include "../src/client.h"
#include "../src/commands/client/client_command_handlers.h"
#include "../src/commands/common/command_defs.h"
#include "../src/response_defs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int saved_stdout;
    FILE *file;
} stdout_capture_t;

static void capture_stdout_begin(stdout_capture_t *capture)
{
    fflush(stdout);
    capture->saved_stdout = dup(STDOUT_FILENO);
    assert(capture->saved_stdout >= 0);

    capture->file = tmpfile();
    assert(capture->file != NULL);

    assert(dup2(fileno(capture->file), STDOUT_FILENO) >= 0);
}

static char *capture_stdout_end(stdout_capture_t *capture)
{
    fflush(stdout);
    assert(dup2(capture->saved_stdout, STDOUT_FILENO) >= 0);
    close(capture->saved_stdout);

    const long end = ftell(capture->file);
    assert(end >= 0);
    rewind(capture->file);

    char *output = malloc((size_t)end + 1);
    assert(output != NULL);

    const size_t read = fread(output, 1, (size_t)end, capture->file);
    assert(read == (size_t)end);
    output[read] = '\0';

    fclose(capture->file);
    return output;
}

static void write_all(const int fd, const unsigned char *data, const size_t len)
{
    size_t written = 0;
    while (written < len) {
        const ssize_t n = write(fd, data + written, len - written);
        assert(n > 0);
        written += (size_t)n;
    }
}

static client_t *test_client(const int fd)
{
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    client_t *client = init_client(fd, ss, UNIX);
    assert(client != NULL);
    return client;
}

static char *handle_response(const unsigned char *frame, const size_t frame_len,
                             const bool benchmark_mode)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    client_t *client = test_client(fds[0]);
    client->benchmark_mode = benchmark_mode;
    write_all(fds[1], frame, frame_len);
    assert(shutdown(fds[1], SHUT_WR) == 0);

    stdout_capture_t capture;
    capture_stdout_begin(&capture);
    command_response_handler(client);
    char *output = capture_stdout_end(&capture);

    close(fds[1]);
    close(client->fd);
    free_client(client);
    return output;
}

static char *handle_response_with_stale_buffer(const unsigned char *frame,
                                               const size_t frame_len)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    client_t *client = test_client(fds[0]);
    client->buffer[3] = 0x00;
    client->buffer[4] = 0x01;
    client->buffer[5] = 'x';
    write_all(fds[1], frame, frame_len);
    assert(shutdown(fds[1], SHUT_WR) == 0);

    stdout_capture_t capture;
    capture_stdout_begin(&capture);
    command_response_handler(client);
    char *output = capture_stdout_end(&capture);

    close(fds[1]);
    close(client->fd);
    free_client(client);
    return output;
}

static char *handle_response_with_prefilled_buffer(const unsigned char *frame,
                                                   const size_t frame_len)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    client_t *client = test_client(fds[0]);
    memset(client->buffer, 'x', sizeof(client->buffer));
    write_all(fds[1], frame, frame_len);
    assert(shutdown(fds[1], SHUT_WR) == 0);

    stdout_capture_t capture;
    capture_stdout_begin(&capture);
    command_response_handler(client);
    char *output = capture_stdout_end(&capture);

    close(fds[1]);
    close(client->fd);
    free_client(client);
    return output;
}

static void assert_output(const unsigned char *frame, const size_t frame_len,
                          const char *expected)
{
    char *output = handle_response(frame, frame_len, false);
    assert(strcmp(output, expected) == 0);
    free(output);
}

static void test_error_response_prints_nil(void)
{
    const unsigned char frame[] = {0x00, 0x01, STATUS_FAILURE};
    assert_output(frame, sizeof(frame), "(nil) \n");
    printf("test_error_response_prints_nil passed.\n");
}

static void test_ok_response_ignores_stale_payload_bytes(void)
{
    const unsigned char frame[] = {0x00, 0x01, STATUS_SUCCESS};
    char *output = handle_response_with_stale_buffer(frame, sizeof(frame));
    assert(strcmp(output, "OK\n") == 0);
    free(output);
    printf("test_ok_response_ignores_stale_payload_bytes passed.\n");
}

static void test_value_response_prints_quoted_payload(void)
{
    const unsigned char frame[] = {
        0x00, 0x06, STATUS_SUCCESS, 0x00, 0x03, 'f', 'o', 'o',
    };
    assert_output(frame, sizeof(frame), "\"foo\" \n");
    printf("test_value_response_prints_quoted_payload passed.\n");
}

static void test_ping_without_payload_prints_pong(void)
{
    const unsigned char frame[] = {0x00, 0x03, CMD_PING, 0x00, 0x00};
    assert_output(frame, sizeof(frame), "PONG\n");
    printf("test_ping_without_payload_prints_pong passed.\n");
}

static void test_ping_with_payload_prints_quoted_payload(void)
{
    const unsigned char frame[] = {
        0x00, 0x05, CMD_PING, 0x00, 0x02, 'h', 'i',
    };
    assert_output(frame, sizeof(frame), "\"hi\" \n");
    printf("test_ping_with_payload_prints_quoted_payload passed.\n");
}

static void test_keys_empty_response_prints_empty_list(void)
{
    const unsigned char frame[] = {0x00, 0x03, CMD_KEYS, 0x00, 0x00};
    assert_output(frame, sizeof(frame), "(empty list)\n");
    printf("test_keys_empty_response_prints_empty_list passed.\n");
}

static void test_keys_response_prints_list_payload(void)
{
    const unsigned char frame[] = {
        0x00, 0x10, CMD_KEYS, 0x00, 0x0d, '1', ')', ' ', 'f',
        'o',  'o',  '\n',     '2',  ')',  ' ',  'b', 'a', 'r',
    };
    assert_output(frame, sizeof(frame), "1) foo\n2) bar\n");
    printf("test_keys_response_prints_list_payload passed.\n");
}

static void test_info_response_uses_declared_payload_length(void)
{
    const unsigned char frame[] = {
        0x00, 0x06, CMD_INFO, 0x00, 0x03, 'a', 'b', 'c',
    };
    char *output = handle_response_with_prefilled_buffer(frame, sizeof(frame));
    assert(strcmp(output, "abc\n") == 0);
    free(output);
    printf("test_info_response_uses_declared_payload_length passed.\n");
}

static void test_benchmark_mode_suppresses_regular_output(void)
{
    const unsigned char frame[] = {
        0x00, 0x06, STATUS_SUCCESS, 0x00, 0x03, 'f', 'o', 'o',
    };
    char *output = handle_response(frame, sizeof(frame), true);
    assert(strcmp(output, "") == 0);
    free(output);
    printf("test_benchmark_mode_suppresses_regular_output passed.\n");
}

static void test_malformed_value_length_is_dropped(void)
{
    const unsigned char frame[] = {
        0x00, 0x06, STATUS_SUCCESS, 0x00, 0x0a, 'f', 'o', 'o',
    };
    assert_output(frame, sizeof(frame), "");
    printf("test_malformed_value_length_is_dropped passed.\n");
}

static void test_truncated_frame_is_dropped(void)
{
    const unsigned char frame[] = {0x00};
    assert_output(frame, sizeof(frame), "");
    printf("test_truncated_frame_is_dropped passed.\n");
}

int main(void)
{
    test_error_response_prints_nil();
    test_ok_response_ignores_stale_payload_bytes();
    test_value_response_prints_quoted_payload();
    test_ping_without_payload_prints_pong();
    test_ping_with_payload_prints_quoted_payload();
    test_keys_empty_response_prints_empty_list();
    test_keys_response_prints_list_payload();
    test_info_response_uses_declared_payload_length();
    test_benchmark_mode_suppresses_regular_output();
    test_malformed_value_length_is_dropped();
    test_truncated_frame_is_dropped();
    return 0;
}
