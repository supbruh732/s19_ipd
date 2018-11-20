#include "types.h"
#include "param.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fs.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "memlayout.h"

#define T_DIR ((1))

  void
procfs_ipopulate(struct inode* ip)
{
  ip->size = 0;
  ip->flags |= I_VALID;

  // inum < 10000 are reserved for directories
  // use inum > 10000 for files in procfs
  ip->type = ip->inum < 10000 ? T_DIR : 100;
}

  void
procfs_iupdate(struct inode* ip)
{
}

  static int
procfs_writei(struct inode* ip, char* buf, uint offset, uint count)
{
  return -1;
}

  static void
sprintuint(char* buf, uint x)
{
  uint stack[10];
  uint stack_size = 0;
  if (x == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  while (x) {
    stack[stack_size++] = x % 10u;
    x /= 10u;
  }
  uint buf_size = 0;
  while (stack_size) {
    buf[buf_size++] = '0' + stack[stack_size - 1];
    stack_size--;
  }
  buf[buf_size] = 0;
}

  static void
sprintx32(char * buf, uint x)
{
  buf[0] = x >> 28;
  for (int i = 0; i < 8; i++) {
    uint y = 0xf & (x >> (28 - (i * 4)));
    buf[i] = (y < 10) ? (y + '0') : (y + 'a' - 10);
  }
}

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

#define PROCFILES ((2))
struct dirent procfiles[PROCFILES+NPROC+1] = {{10001,"meminfo"}, {10002,"cpuinfo"}};
struct dirent de[PROCFILES + 2] = { {20000, "name"}, {30000, "ppid"}, {40000, "pid"}, {50000, "mappings"}};

// returns the number of active processes, and updates the procfiles table
  static uint
updateprocfiles()
{
  int num = 0, index = 0;
  acquire(&ptable.lock);
  while (index < NPROC) {
    if (ptable.proc[index].state != UNUSED && ptable.proc[index].state != ZOMBIE) {
      procfiles[PROCFILES+num].inum = index+1;
      ptable.proc[index].cwd->inum = 10000 + index;
      //cprintf("%d\n", (int)ptable.proc[index].cwd->inum);
      sprintuint(procfiles[PROCFILES+num].name,ptable.proc[index].pid);
      num++;
      
      // also checks if the process at [index] is the current process
      // if yes, create a "self" directory
      // TODO: your code here
      if(ptable.proc[index].pid == proc->pid) {
        (procfiles[5]).inum = index+1;
        ptable.proc[index].cwd->inum = 10000 + index;
        strncpy(procfiles[5].name, "self", 4);
        num++;
      }

    }
    index++;
  }
  release(&ptable.lock);
  return PROCFILES + num;
}

  static int
readi_helper(char * buf, uint offset, uint maxsize, char * src, uint srcsize)
{
  if (offset > srcsize)
    return -1;
  uint end = offset + maxsize;
  if (end > srcsize)
    end = srcsize;
  memmove(buf, src+offset, end-offset);
  return end-offset;
}

// returns the number of directories in a process
static uint
updatedde(struct inode* ip)
{
  int num = 0;
  //acquire(&ptable.lock);
    de[0].inum = 20000 + (int)ip->inum % 10000;
    de[1].inum = 30000 + (int)ip->inum % 10000;
    de[2].inum = 40000 + (int)ip->inum % 10000;
    de[3].inum = 50000 + (int)ip->inum % 10000;
  //release(&ptable.lock);
  return PROCFILES + 2;
}

int
procfs_readi(struct inode* ip, char* buf, uint offset, uint size)
{
  const uint procsize = sizeof(struct dirent)*updateprocfiles();
  //cprintf("%d\n", (int)ip->inum - 10000);
  //const uint desize = sizeof(struct dirent)*updatedde((int)ip->inum - 10000);
  // the mount point
  const uint desize = sizeof (struct dirent) * updatedde(ip);
  if (ip->mounted_dev == 2) {

    return readi_helper(buf, offset, size, (char *)procfiles, procsize);
  }

  // directory - can only be one of the process directories
  if (ip->type == T_DIR) {
    
    // List the files in a process directory:
    // It contains "name", "pid", "ppid", and "mappings".
    // Choose a good pattern for inum.
    // You will need to check inum to see what should be the file content (see below)
    // TODO: Your code here

    //cprintf("%d ", (int)ip->inum % 10000);
    return (readi_helper(buf,offset, size,(char*)de, desize));

  }

  // files
  char buf1[32];
  switch (((int)ip->inum)) {
    case 10001: // meminfo: print the number of free pages
      sprintuint(buf1, kmemfreecount());
      return readi_helper(buf, offset, size, buf1, strlen(buf1));
    case 10002: // cpuinfo: print the total number of cpus. See the 'ncpu' global variable
      // TODO: Your code here
      sprintuint(buf1, ncpu);
      return readi_helper(buf, offset, size, buf1, strlen(buf1));
      //return 0;
    default: break;
  }

  // filling the content for all other files
  // TODO: Your code here
  
  int index = (int)ip->inum % 10000;
  //cprintf("%d\n", index);
  //cprintf("%d\n", (int)ip->inum / 10000);

  switch(((int)ip->inum) / 10000) {
     case 2: //name of the file
      cprintf("%s", ptable.proc[index-1].name);
      return 0;
    case 3:    //ppid
      sprintuint(buf1, ptable.proc[index-1].parent->pid);
      return readi_helper(buf, offset, size, buf1, strlen(buf1));
    case 4:   //pid
      sprintuint(buf1, ptable.proc[index-1].pid);
      return readi_helper(buf, offset, size, buf1, strlen(buf1));
    case 5:   //mappings

      //buf1[0] = "\0";
      strncpy(buf1, "", 1);

      for(addr_t i = 0; i < ptable.proc[index-1].sz; i+=PGSIZE) {

        //sprintx32(buf1, ptable.proc[index-1].sz);
        //return readi_helper(buf, offset, size, buf1, strlen(buf1));
        
        pde_t *pgdir = ptable.proc[index-1].pgdir;
        pte_t *pte;
        pte = walkpg(pgdir, i, 0);
        
        //cprintf("HERE 1\n");
        addr_t pa = PTE_ADDR(pte);

        sprintx32(buf1, i);
        cprintf("%s ", buf1);
        sprintx32(buf1, pa);
        cprintf("%s\n", buf1);

        //cprintf("%s\n", buf1);
        //return readi_helper(buf, offset, size, buf1, strlen(buf1));
      }
      return 0;
  }

  return -1; // return -1 on error
}

struct inode_functions procfs_functions = {
  procfs_ipopulate,
  procfs_iupdate,
  procfs_readi,
  procfs_writei,
};

  void
procfsinit(char * const path)
{
  begin_op();
  struct inode* mount_point = namei(path);
  if (mount_point) {
    ilock(mount_point);
    mount_point->i_func = &procfs_functions;
    mount_point->mounted_dev = 2;
    iunlock(mount_point);
  }
  end_op();
}
