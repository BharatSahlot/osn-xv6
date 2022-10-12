#include "user.h"

int
main(int argc, char* argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: setpriority <new_priority> <pid>\n");
    exit(1);
  }
#if defined(PBS)
  int np = atoi(argv[1]);
  int pid = atoi(argv[2]);
  if(set_priority(np, pid) < 0) {
    fprintf(2, "could not set priority\n");
    exit(1);
  }
  exit(0);
#else
  fprintf(2, "kernel not compiled with Priority Based Scheduling\n");
  exit(1);
#endif
}
