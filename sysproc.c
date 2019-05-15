#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = proc->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

addr_t sys_mmap() {
  
  //cprintf("We are at start\n");
  int fd;
  struct file *f;
  int flags;

  argint(0,&fd);
  f = proc->ofile[fd];
  argint(1, &flags);

  if(fd < 0 || flags < 0 || flags > 1) {
    cprintf("Nothing here\n");
    return -1;
  }
  
  //cprintf("mmapping fd %d with %s\n",fd,(flags==0?"eager":"lazy"));
  //cprintf("We are holding %d apples.\n", 1);

  //eager
  if(flags == 0){

    pde_t* pgdir = proc->pgdir;		//page table
    char* memloc;			//memory location
    
    addr_t old_sz = proc->mmap_sz + MMAPBASE; //old memory size as mmap size + base addr
						                        //old size after read
    addr_t f_sz = PGROUNDUP(old_sz + (f->ip->size));
    addr_t new_sz = old_sz;		//set old memory as new size -- size after read

    addr_t ptr = PGROUNDUP(old_sz);	//ptr to current page


    /* loop until read all the bytes of the file (currently only the first line)
     * ptr to the current page + 4096 (bytes/page), similarly new_sz
     * allocated phycicall page (memloc = kalloc)
     * do -- memset(location, 0, PGSIZE) --> sets with 0
     * do -- map virtual page to physcial ?? how (mappage???)
     * do -- fileread(file, char address, int??)
    */
    for(; ptr < f_sz; ptr+= PGSIZE){
      memloc = kalloc();

      if(memloc == 0){
        cprintf("mmap out of memory\n");
        return -1;
      }

      memset(memloc, 0, PGSIZE);	//write 0 to memory space
      mappages(pgdir, (char*)ptr, PGSIZE, V2P(memloc), PTE_W|PTE_U);
					//map physical to virtual and give file permissions
      
      fileread(f, (char*)ptr, PGSIZE);	//read the file from current page & for 4096 buffer
	  new_sz += PGSIZE;					//increment new size by page size
	}
    

    proc->mmap_sz = new_sz - MMAPBASE;	//set mapping size to be new - base
    switchuvm(proc);
    return old_sz;			//return the old address so that next line can read
					        //store here
  } else if (flags == 1) {

    /* Update the meta data for lazy
     * Probably should save the file, page address where it begins and ends
     * Possibly in a struct??
    */
    addr_t old_sz = proc->mmap_sz + MMAPBASE; //old memory size as mmap size + base addr
	                                  //old size after read
    int count = proc->faults;
    
    struct lazy_data *data = &(proc->meta[count]);

    data->start = proc->mmap_sz + MMAPBASE;       // sets the start of the fault as the mapping size
    data->end = PGROUNDUP(data->start + f->ip->size);     // sets the end of the mapping as the file size
    data->fd = fd;
    //data->f = f;

    proc->mmap_sz += (data->end - data->start);       // new mapping size
    
    proc->faults++;
    return old_sz;
  }

  return 0;
}

/* Sig Handler for Page Fault --> T_PGFL -- value = 14
 * Allocate the here to fix the fault??
 * do -- get the address (line) where the fault occurs using rc2()
 * do -- allocate the kernal space here to fix the fault??
 * do -- if address found do the one iteration of the loop from eager
*/
int page_fault(addr_t addrs){
  
  int isFault = 0;               //to see if the address lies in the mapped process
  struct lazy_data *data;        //used to check the address line

  int i;
  for(i = 0; i < proc->faults; ++i) {
    data = &proc->meta[i];       //get the lazy mapped process data
    
    //check the address lies between start and end of the lazy map
    if(addrs >= data->start && addrs <= data->end){
      isFault = 1;            //found the range
      break;
    }
  }
  
  if(!isFault){
    return -1;            // no lazy mapped file found
  }
  
  /* cprintf("start: %p\n", data->start);
  cprintf("end: %p\n", data->end);
  cprintf("addrs: %p\n", addrs);*/

  //same as the eager map... fixes the fault
  struct file *f = proc->ofile[data->fd];     //opens the file that has the fault
  pde_t* pgdir = proc->pgdir;

  addr_t ptr = PGROUNDDOWN(addrs);
  char* memloc = kalloc();

  if(memloc == 0) {
    return -1;
  }
  
  addr_t offset = ptr - data->start;
  memset(memloc, 0, PGSIZE);
  mappages(pgdir, (char*)ptr, PGSIZE, V2P(memloc), PTE_W|PTE_U);

  //addr_t offset = ptr - data->start;

  fileread(f, (char*)ptr, PGSIZE);

  switchuvm(proc);
  return 1;

}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
