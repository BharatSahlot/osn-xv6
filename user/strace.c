#include "user.h"

int
main(int argc, char* argv[])
{
  if(argc < 3){
    fprintf(2, "Usage: strace <integer-mask> command...\n");
    exit(1);
  }
  int mask = atoi(argv[1]);
  int id = fork();
  if(id < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(id == 0){
    trace(mask);
    if(exec(argv[2], &argv[2]) < 0)
    {
      fprintf(2, "exec failed\n");
      exit(1);
    }
  }
  wait(&id);
  exit(0);
}
