#include "types.h"
#include "stat.h"
#include "user.h"

#define MAX_NAME_LEN 1024  // Larger than typical process names

int main(int argc, char* argv[]) {
  char parent_name[MAX_NAME_LEN];
  char child_name[MAX_NAME_LEN];

  if (getparentname(parent_name, child_name, MAX_NAME_LEN, MAX_NAME_LEN) == 0) {
    printf(1, "XV6_TEST_OUTPUT Large buffers handled correctly.\n");
    exit();
  }

  printf(2, "XV6_TEST_ERROR Test failed! Large buffer size not handled correctly.\n");
  exit();
}
