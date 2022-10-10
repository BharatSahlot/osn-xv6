#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
#if !defined(MLFQ)
  printf("kernel not compiled with MLFQ\n");
  exit(1);
#else
  int f = fork();
  if(f == 0)
  {
    if(exec(argv[1], &argv[1]) < 0)
    {
      fprintf(2, "exec failed\n");
      exit(1);
    }
    exit(0);
  }
  else
  {
    while(1)
    // for(int i = 0; i < 1000*500000; i++)
    {
      // if((i % 1000000) == 0)
      // {
        write(1, "1", 1);
      // }
    }
    int stat;
    wait(&stat);
  }
  write(1, "\n", 1);
  return 0;
#endif
}
