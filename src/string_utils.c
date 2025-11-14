#include "string_utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *to_upper(const char *string) {
  char *result = malloc(strlen(string) + 1);
  for (int i = 0; i < strlen(string); ++i) {
    result[i] = toupper(string[i]);
  }
  result[strlen(string)] = '\0';
  return result;
}