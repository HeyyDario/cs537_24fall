#include "types.h"
#include "stat.h"
#include "user.h"

#define SMALL_BUF_LEN 5  // Small buffer to test truncation

int main(int argc, char* argv[]) {
  char parent_name[SMALL_BUF_LEN];
  char child_name[SMALL_BUF_LEN];

  if (getparentname(parent_name, child_name, SMALL_BUF_LEN, SMALL_BUF_LEN) == 0) {
    printf(1, "XV6_TEST_OUTPUT Truncated Parent Name: %s, Truncated Child Name: %s\n", parent_name, child_name);
    exit();
  }

  printf(2, "XV6_TEST_ERROR Test failed! Truncated buffer size not handled correctly.\n");
  exit();
}
