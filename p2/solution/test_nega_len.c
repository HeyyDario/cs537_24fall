// test with negative buffer lengths

#include "types.h"
#include "stat.h"
#include "user.h"

#define MAX_NAME_LEN 256

int main(int argc, char* argv[]) {
  char parent_name[MAX_NAME_LEN];
  char child_name[MAX_NAME_LEN];

  int negative_buffer_length = -5;

  if (getparentname(parent_name, child_name, negative_buffer_length, MAX_NAME_LEN) < 0 &&
      getparentname(parent_name, child_name, MAX_NAME_LEN, negative_buffer_length) < 0) {
    printf(1, "XV6_TEST_OUTPUT Negative buffer length handled correctly.\n");
    exit();
  }

  printf(2, "XV6_TEST_ERROR Test failed! Negative buffer length not handled correctly.\n");
  exit();
}
