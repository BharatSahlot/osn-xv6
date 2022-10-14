#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char** argv) {
  if(argc != 2) {
    fprintf(2, "Usage: cpubound <no-of-forks> \n");
    exit(1);
  }
  int N = atoi(argv[1]);

  int n, pid;
  for (n = 0; n < N; n++) {
    pid = fork();
    if (pid < 0)
      break;
    if (pid == 0) {
      for (long long int i = 0; i < 20000000000; i++) {}; // CPU bound process
      exit(0);
    }
  }
  for(;n > 0; n--) {
      wait(0);
  }
  exit(0);
}
