#include <stdarg.h>

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "stat.h"
#include "vga.h"

#define MAX_ADD 64000			//number of bytes in the screen

static char buf[MAX_ADD*2];		//stores the console text
static ushort *video = (ushort*)P2V(0xa0000);   //write img bytes starting here
static char *crt = (char*)P2V(0xb8000);	//CGA memory

static int cur = 0;		//current position of the pixel cursor


static struct {
  struct spinlock lock;
  int locking;
} disp;


#define CRTPORT 0x3d4

#define INPUT_BUF 128
struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} dis_in;


static void cgaputc(int c, int i)
{
  crt[i++] = c;
}


static int pos;

int displayioctl(struct file *f, int param, int value){

  if(param == 1 && value == 0x13){
    
    /*read each byte and store in buf*/
    for(int i = 0; i < MAX_ADD*2; i++){
      buf[i] = crt[i];
    }

    vgaMode13();

  } else if(param == 1 && value == 0x3){
    
    /*read each byte from buf and store in CGA mem*/
    vgaMode3();
    for(int i = 0; i < MAX_ADD*2; i++){
      //crt[i] = buf[i];
      cgaputc(buf[i], i);
    }

  } else if(param == 2){
    //palette colors
    int index = value >> 24;
    int r = (value & 0x00ff0000) >> 16;
    int g = (value & 0x0000ff00) >> 8;
    int b = (value & 0x000000ff);
    
     vgaSetPalette(index, r, g, b);
  }
  
  return 0;
}

/*int c = has the byte value of the image
  int pos = place the pixel needs to be written to
  int cur = current position.
*/
static void vgaputc(int c, int pos, int cur){

  video[(cur+pos)] = c;		//current + pos = pix location 

}

int displaywrite(struct file *f, char *buf, int n){
  
  acquire(&disp.lock);
  for(int i = 0; i < n; i++){
    vgaputc(buf[i] & 0xff, i/2, cur);		//(byte value, offset, current cursor)
  }
  release(&disp.lock);

  cur += n/2;			//set the curr to be next pix line
  cur = cur % MAX_ADD;		//set absolute value of the pix line #
  return n;			//return the number of lines written
}


void displayinit(void) {

  initlock(&disp.lock, "display");
  initlock(&dis_in.lock, "input");

  devsw[DISPLAY].write = displaywrite;
  //devsw[DISPLAY].read = consoleread;
  disp.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}
