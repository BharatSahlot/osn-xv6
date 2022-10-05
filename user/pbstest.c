#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

// does the error path in open() for attempt to write a
// directory call iput() in a transaction?
// needs a hacked kernel that pauses just after the namei()
// call in sys_open():
//    if((ip = namei(path)) == 0)
//      return -1;
//    {
//      int i;
//      for(i = 0; i < 10000; i++)
//        yield();
//    }
void
openiputtest(char *s)
{
  int pid, xstatus;

  if(mkdir("oidir") < 0){
    printf("%s: mkdir oidir failed\n", s);
    exit(1);
  }
  printf("1\n");
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  printf("2\n");
  if(pid == 0){
    int fd = open("oidir", O_RDWR);
    if(fd >= 0){
      printf("%s: open directory for write succeeded\n", s);
      exit(1);
    }
    exit(0);
  }
  printf("3\n");
  sleep(1);
  if(unlink("oidir") != 0){
    printf("%s: unlink failed\n", s);
    exit(1);
  }
  printf("4\n");
  wait(&xstatus);
  printf("5\n");
  exit(xstatus);
}

int main(){
  int i = fork();
  if(i < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(i == 0){
    openiputtest("bum");
    exit(0);
  }
  wait(&i);
  exit(0);
}
