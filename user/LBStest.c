#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
#if !defined(LBS)
  printf("kernel not compiled with LBS\n");
  exit(1);
#else
  int t1 = atoi(argv[1]);
  int t2 = atoi(argv[2]);
  if(t1 < 0 || t2 < 0){
    exit(1);
  }
  int f = fork();
  if(f == 0)
  {
    settickets(t1);
    for(int i = 0; i < 1000*500000; i++)
    {
      if((i % 1000000) == 0)
      {
        write(2, "0", 1);
      }
    }
    exit(0);
  }
  else
  {
    settickets(t2);
    for(int i = 0; i < 1000*500000; i++)
    {
      if((i % 1000000) == 0)
      {
        write(2, "1", 1);
      }
    }
    int stat;
    wait(&stat);
  }
  write(2, "\n", 1);
  return 0;
#endif
}
