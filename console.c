// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

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

//static void consputc(int);
static void consputc(int, int);
struct file out;

static int panicked = 0;
int glb = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static char digits[] = "0123456789abcdef";

static void
printptr(addr_t x) {
  int i;
  for (i = 0; i < (sizeof(addr_t) * 2); i++, x <<= 4)
    //consputc(digits[x >> (sizeof(addr_t) * 8 - 4)]);
    consputc(digits[x >> (sizeof(addr_t) * 8 - 4)], 0x0700);
}

static void
printint(int xx, int base, int sign)
{
  char buf[32];
  int i;
  uint64 x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    //consputc(buf[i]);
    consputc(buf[i], 0x0700);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  va_start(ap, fmt);

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      //consputc(c);
      consputc(c, 0x0700);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 0);
      break;
    case 'p':
      printptr(va_arg(ap, addr_t));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        //consputc(*s);
        consputc(*s, 0x0700);
      break;
    case '%':
      //consputc('%');
      consputc('%', 0x0700);
      break;
    default:
      // Print unknown % sequence to draw attention.
      //consputc('%');
      consputc('%', 0x0700);
      //consputc(c);
      consputc(c, 0x0700);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  addr_t pcs[10];
  
  cli();
  cons.locking = 0;
  cprintf("cpu%d: panic: ", cpu->id);
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
//cgaputc(int c)
cgaputc(int c, int color)
{
  int pos;
  
  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } else {
      //Checks if we are changing global color or
      //file descriptor color.
      if(glb == 0){
        crt[pos++] = (c&0xff) | color;  // black on white, 0x0700 needs to change to print color
      } else {
        crt[pos++] = (c&0xff) | out.color;  //global color
      }
  }  
  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }
  
  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  crt[pos] = ' ' | 0x0700;
}

void
//consputc(int c)
consputc(int c, int color)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c, color);
}


#define INPUT_BUF 128
struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c;

  acquire(&input.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('Z'): // reboot
      lidt(0,0);
      break;
    case C('P'):  // Process listing.
      procdump();
      break;
    case C('U'):  // Kill line.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        //consputc(BACKSPACE);
        consputc(BACKSPACE, 0x0700);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.e != input.w){
        input.e--;
        //consputc(BACKSPACE);
        consputc(BACKSPACE, 0x0700);
      }
      break;
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        //consputc(c);
	consputc(c,0x0700);
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&input.lock);
}

int
consoleread(struct file *f, char *dst, int n)
{
  uint target;
  int c;

  target = n;
  acquire(&input.lock);
  while(n > 0){
    while(input.r == input.w){
      if(proc->killed){
        release(&input.lock);
        ilock(f->ip);
        return -1;
      }
      sleep(&input.r, &input.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&input.lock);

  return target - n;
}

int
consoleioctl(struct file *f, int param, int value) {  
  //cprintf(1, "Change color\n");

  //value = value<<8;
  
  if(param == 1){
    glb = 1;
    out.color = value<<8;
  } else {
    f->color = value<<8;
    glb = 0;
  }

  //out.color = value;
  if(f->color == value<<8 || out.color == value<<8){
    return 0;
  } else {
    cprintf("Got unknown console ioctl request. %d = %d\n",param,value);
    return -1;
  }
}


int
consolewrite(struct file *f, char *buf, int n)
{
  int i;  

  if(f->color < 0x0100){
    f->color = 0x0700;
  }

  /*if(change != 0){
    f->color = out.color
  } else {
    f->color = 0x0700;
  }*/

  acquire(&cons.lock);
  for(i = 0; i < n; i++){
    //consputc(buf[i] & 0xff);
    /*if(glb == 1){
      consputc(buf[i] & 0xff, out.color);
    } else {
      consputc(buf[i] & 0xff, f->color);
    }*/
    consputc(buf[i] & 0xff, f->color);
  }
  release(&cons.lock);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");
  initlock(&input.lock, "input");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

