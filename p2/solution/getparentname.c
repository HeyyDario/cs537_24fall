// Simple getparentname.

#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  char parentbuf[256];
  char childbuf[256];

  // Call the getparentname system call
  if (getparentname(parentbuf, childbuf, sizeof(parentbuf), sizeof(childbuf)) < 0) {
    printf(1, "Error: getparentname system call failed\n");
    return -1;
  }
  
  printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s\n", parentbuf, childbuf);

  return 0;
}
