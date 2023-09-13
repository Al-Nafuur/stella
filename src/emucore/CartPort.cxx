//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2023 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#define BCM2708_PERI_BASE        0x3F000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

#define SYSTEM_TIMER_OFFSET 0x3000
#define ST_BASE (BCM2708_PERI_BASE + SYSTEM_TIMER_OFFSET)


#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "M6532.hxx"
#include "TIA.hxx"
#include "System.hxx"
#include "CartPort.hxx"

#define BLOCK_SIZE (4*1024)
// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define GET_DATA_BUS() (*(gpio+13)&0x1fe000)>>13 // GPIO 13 - 20 ( 0b000111111110000000000000 )

#define SET_DATA_BUS_READ()  *(gpio+(1)) = 0b00000000000000000000000001001001; \
                             *(gpio+(2)) = 0b001000; \
                             GPIO_CLR = 1<<21;
#define SET_DATA_BUS_WRITE() *(gpio+(1)) = 0b00001001001001001001001001001001; \
                             *(gpio+(2)) = 0b001001; \
                             GPIO_SET = 1<<21;

#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CartridgePort::CartridgePort(const ByteBuffer& image, size_t size,
                         string_view md5, const Settings& settings,
                         size_t bsSize)
  : CartridgeEnhanced(image, 4_KB, md5, settings, 4_KB)
{
  myDirectPeek = false;
}
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgePort::install(System& system)
{
  CartridgeEnhanced::install(system);

  // We need to claim all ! access to it here, and deal with it in peek/poke below
  const System::PageAccess access(this, System::PageAccessType::READWRITE);
  for(uInt16 addr = 0x0000; addr < 0x2000; addr += System::PAGE_SIZE)
    mySystem->setPageAccess(addr, access);

  // Set up a memory regions to access GPIO
  //
   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit(-1);
   }

   /* mmap GPIO */
   gpio_map = mmap(
      NULL,             //Any adddress in our space will do
      BLOCK_SIZE,       //Map length
      PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
      MAP_SHARED,       //Shared with other processes
      mem_fd,           //File to map
      GPIO_BASE         //Offset to GPIO peripheral
   );

   close(mem_fd); //No need to keep mem_fd open after mmap

   if (gpio_map == MAP_FAILED) {
      printf("mmap error %d\n", (int)gpio_map);//errno also set!
      exit(-1);
   }

   // Always use volatile pointer!
   gpio = (volatile unsigned *)gpio_map;


  if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit(-1);
  }

  system_timer = mmap(
      NULL,
      4096,
      PROT_READ | PROT_WRITE,
      MAP_SHARED,
      mem_fd,
      ST_BASE
  );

  close(mem_fd);

  if (system_timer == MAP_FAILED) {
      printf("mmap error %d\n", (int)system_timer);  // errno also set!
      exit(-1);
  }
  mySystemTimer = (volatile system_timer_t*)system_timer;

  // Set GPIO pins 0-12 to output (6502 address)
  *(gpio+(0)) = 0b00001001001001001001001001001001;
  *(gpio+(1)) = 0b001001001;

  // Set GPIO pin 21 to output (Level shifter dir)
  *(gpio+(2)) = 0b001000;

  // Set GPIO pin 21 to Low (ls dir read)
  GPIO_CLR = 1<<21;

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgePort::reset()
{
//   printf("start reset CartridgePort\n");

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 CartridgePort::peek(uInt16 address)
{
  uInt8 result = 0;

  if(address & 0x1000 ){ // check if cartport address
    SET_DATA_BUS_READ() // we can restore the databus after the write, or set it before every read/write
  // First tell the cartridge wich adress is requested
    GPIO_CLR = 0b1111111111111;
    GPIO_SET = address;
    t0 = mySystemTimer->counter_low;
    myNanoSleep();
    result = GET_DATA_BUS();
  }else{ // TIA, RIOT or RAM read.
    if(address & 0b10000000 ){
      result = mySystem->m6532().peek(address);
    }else{
      result = mySystem->tia().peek(address);
    }
    // and of course we have to set the databus here for
    // the Cart to peek what TIA and RIOT have to say!
    uInt32 gpio_value = (uInt32) address | (((uInt32)result)<<13 );
    SET_DATA_BUS_WRITE()
    GPIO_CLR = 0b111111111111111111111;
    GPIO_SET = gpio_value;
    t0 = mySystemTimer->counter_low;
    myNanoSleep();

//    SET_DATA_BUS_READ() // we can restore the databus after the write, or set it before every read/write
  }

  return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgePort::poke(uInt16 address, uInt8 value)
{
//   printf("start poke CartridgePort\n");
  uInt32 gpio_value = (uInt32) address | (((uInt32)value)<<13 );

  SET_DATA_BUS_WRITE()
  GPIO_CLR = 0b111111111111111111111;
  GPIO_SET = gpio_value;
  t0 = mySystemTimer->counter_low;

  if(! (address & 0x1000) ){ // check if TIA, RIOT or RAM write.
    if(address & 0b10000000 ){
      mySystem->m6532().poke(address, value);
    }else{
      mySystem->tia().poke(address, value);
    }
  }
  myNanoSleep();
//  SET_DATA_BUS_READ()  // we can restore the databus after the write, or set it before every read/write

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgePort::patch(uInt16 address, uInt8 value)
{
  // For now, we ignore attempts to patch the port address space
  printf("start patch CartridgePort %d\n",address);
  return false;
}

bool CartridgePort::save(Serializer& out) const
{
  // For now, we ignore attempts to patch the port address space
//  printf("start save CartridgePort\n");
  return false;
}
bool CartridgePort::load(Serializer& in)
{
  // For now, we ignore attempts to patch the port address space
//  printf("start load CartridgePort\n");
  return false;
}

//#pragma GCC push_options
//#pragma GCC optimize("O0")
//__attribute__((optimize(0)))
void CartridgePort::myNanoSleep() // static inline void?
{
// v1
//  nanosleep(&ts, NULL);

// v2
//  i = 0;
//  while(i < 10000)
//    i++;

// v3 lons
//    long end_t = t_start.tv_nsec + 800;
//    clock_settime(CLOCK_THREAD_CPUTIME_ID, &ts);
//    do{
//      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_stop);
//    } while( t_stop.tv_nsec < 200000);

// v4
  do{
    t1 = mySystemTimer->counter_low - t0;
  } while( t1 < 2);


}
//#pragma GCC pop_options
