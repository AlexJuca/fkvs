#include "../src/client.h"
#include "../src/commands/common/command_registry.h"
#include "../src/response_defs.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void set_nonblocking_fd(const int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

static void fill_send_buffer_until_eagain(const int fd)
{
    unsigned char buf[4096];
    memset(buf, 'z', sizeof(buf));

    for (size_t sent = 0; sent < 16U * 1024U * 1024U;) {
        ssize_t n = send(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }

        assert(n < 0);
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        return;
    }

    assert(!"socket send buffer did not fill");
}

static size_t drain_available(const int fd, unsigned char *out,
                              const size_t cap, const bool collect)
{
    size_t used = 0;
    for (;;) {
        unsigned char buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (collect) {
                assert(used + (size_t)n <= cap);
                memcpy(out + used, buf, (size_t)n);
                used += (size_t)n;
            }
            continue;
        }

        assert(n < 0);
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        return used;
    }
}

static void flush_and_collect(client_t *client, const int read_fd,
                              unsigned char *out, size_t *used,
                              const size_t cap)
{
    for (size_t attempts = 0; attempts < 128 && client->wbuf_used > 0;
         attempts++) {
        wbuf_flush(client);
        *used += drain_available(read_fd, out + *used, cap - *used, true);
    }
}

static void test_backpressured_append_keeps_later_frames(void)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    int sndbuf = 1024;
    (void)setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    set_nonblocking_fd(fds[0]);
    set_nonblocking_fd(fds[1]);

    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    client_t *client = init_client(fds[0], ss, UNIX);
    assert(client != NULL);

    enum {
        payload_len = 65530,
        large_frame_len = payload_len + 5,
        ok_frame_len = 3,
        expected_len = large_frame_len + ok_frame_len
    };

    unsigned char *payload = malloc(payload_len);
    assert(payload != NULL);
    memset(payload, 'a', payload_len);

    send_reply(client, payload, payload_len);
    free(payload);
    assert(client->wbuf_used == large_frame_len);

    fill_send_buffer_until_eagain(client->fd);
    send_ok(client);

    drain_available(fds[1], NULL, 0, false);

    unsigned char *received = malloc(expected_len);
    assert(received != NULL);
    size_t received_len = 0;
    flush_and_collect(client, fds[1], received, &received_len, expected_len);

    assert(client->wbuf_used == 0);
    assert(received_len == expected_len);
    assert(received[0] == 0xff);
    assert(received[1] == 0xfd);
    assert(received[2] == STATUS_SUCCESS);
    assert(received[expected_len - 3] == 0x00);
    assert(received[expected_len - 2] == 0x01);
    assert(received[expected_len - 1] == STATUS_SUCCESS);

    free(received);
    close(fds[1]);
    free_client(client);

    printf("test_backpressured_append_keeps_later_frames passed.\n");
}

int main(void)
{
    test_backpressured_append_keeps_later_frames();
    return 0;
}
