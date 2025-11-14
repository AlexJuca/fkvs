#include "../src/string_utils.h"

#include <assert.h>
#include <string.h>

void test_string_utils_to_upper() {
    const char *string = "set";
  assert(strcmp(to_upper(string), "SET") == 0);

  const char *ping = "ping ";
  assert(strcmp(to_upper(ping), "PING ") == 0);

  const char *world = "WORLD";
  assert(strcmp(to_upper(world), "WORLD") == 0);
}

int main(int argc, char *argv[]) {
  test_string_utils_to_upper();
}