// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1); // the second argument is CONSOLE = 1
    open("console", O_RDWR);
  }

  dup(0);  // stdout
//  dup(0);  // stderr
  open("console", O_RDWR); // stderr


  if(open("display", O_RDWR) < 0){
    mknod("display", 2, 1); // the second argument is DISPLAY = 2
    open("display", O_RDWR);
  }

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
