#include "../src/string_utils.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void test_string_utils_to_upper()
{
    const char *string = "set";
    char *upper = to_upper(string);
    assert(upper != NULL);
    assert(strcmp(upper, "SET") == 0);
    free(upper);

    const char *ping = "ping ";
    upper = to_upper(ping);
    assert(upper != NULL);
    assert(strcmp(upper, "PING ") == 0);
    free(upper);

    const char *world = "WORLD";
    upper = to_upper(world);
    assert(upper != NULL);
    assert(strcmp(upper, "WORLD") == 0);
    free(upper);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    test_string_utils_to_upper();
}
