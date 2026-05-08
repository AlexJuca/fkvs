#include "../src/server_limits.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void test_accepts_until_configured_client_limit(void)
{
    server_t srv = {.max_clients = 2, .num_clients = 0};

    assert(fkvs_server_can_accept_client(&srv));

    srv.num_clients = 1;
    assert(fkvs_server_can_accept_client(&srv));

    srv.num_clients = 2;
    assert(!fkvs_server_can_accept_client(&srv));

    printf("test_accepts_until_configured_client_limit passed.\n");
}

static void test_rejects_when_limit_is_unconfigured(void)
{
    server_t srv = {.max_clients = 0, .num_clients = 0};

    assert(!fkvs_server_can_accept_client(&srv));
    assert(!fkvs_server_can_accept_client(NULL));

    printf("test_rejects_when_limit_is_unconfigured passed.\n");
}

static void test_rejected_client_accounting_saturates(void)
{
    server_t srv = {.num_disconnected_clients = 0};

    fkvs_server_record_rejected_client(&srv);
    assert(srv.num_disconnected_clients == 1);
    assert(srv.metrics.disconnected_clients == 1);

    srv.num_disconnected_clients = INT_MAX;
    fkvs_server_record_rejected_client(&srv);
    assert(srv.num_disconnected_clients == INT_MAX);
    assert(srv.metrics.disconnected_clients == INT_MAX);

    fkvs_server_record_rejected_client(NULL);

    printf("test_rejected_client_accounting_saturates passed.\n");
}

int main(void)
{
    test_accepts_until_configured_client_limit();
    test_rejects_when_limit_is_unconfigured();
    test_rejected_client_accounting_saturates();
    return 0;
}
