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

#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
//#include <bcm_host.h>


#include "M6532.hxx"
#include "TIA.hxx"
#include "System.hxx"
#include "CartPort.hxx"

// Peripheral base address for the Raspberry Pi3
//#define PI_PERI_BASE  0x3F000000

// Peripheral base address for the Raspberry Pi4
#define PI_PERI_BASE  0xFE000000

#define GPIO_BASE     (PI_PERI_BASE + 0x200000) /* GPIO controller */

#define SYSTEM_TIMER_OFFSET 0x3000
#define ST_BASE (PI_PERI_BASE + SYSTEM_TIMER_OFFSET)

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
                             *(gpio+(2)) = 0b001001000; \
                             GPIO_CLR = 1<<21
#define SET_DATA_BUS_WRITE() *(gpio+(1)) = 0b00001001001001001001001001001001; \
                             *(gpio+(2)) = 0b001001001; \
                             GPIO_SET = 1<<21

#define LOCK_ADDRESS_BUS()   GPIO_CLR = 1<<22
#define UNLOCK_ADDRESS_BUS() GPIO_SET = 1<<22

#define MASK_ADDRESS_BUS 0b000000001111111111111
#define MASK_DATA_BUS    0b111111110000000000000


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

//  cpu_set_t mask;

  myCycleTimerThread = std::thread(&CartridgePort::cycleManagerThread, this);

  // set CPU we want to run on
 // CPU_ZERO(&mask);
 // CPU_SET(2, &mask);
 // int result = sched_setaffinity(0, sizeof(mask), &mask);

//  printf("CPU set result CT %d \n", result );

  lastAccessWasWrite = false;

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

  // Set GPIO pins 0-12 to output (6502 address)
  // pins 13-20 to input (6502 data)
  // pin 21 to output (Level shifter direction)
  // pin 22 to output (Address bus latch lock/unlock)
  *(gpio+(0)) = 0b00001001001001001001001001001001;
  *(gpio+(1)) = 0b001001001;
  *(gpio+(2)) = 0b001001000;

  // Set GPIO pin 21 to low (initial ls direction is read)
  GPIO_CLR = 1<<21;

  // Set GPIO pin 22 to high (initial address bus unlocked)
  GPIO_SET = 1<<22;

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
    LOCK_ADDRESS_BUS();
    uInt32 gpio_addr_value = (uInt32) (address & MASK_ADDRESS_BUS) | 0x400000;
    if(lastAccessWasWrite){
      waitForCycleEnd();
      GPIO_CLR = MASK_ADDRESS_BUS;
      GPIO_SET = gpio_addr_value;

//  int g = 15; // ~30ns delay for the latch
//  while(--g){
//    asm volatile("nop");
//  }

      SET_DATA_BUS_READ(); // delete Data on Bus not before changing the address!!!!!
    }else{
      GPIO_CLR = MASK_ADDRESS_BUS;
      GPIO_SET = gpio_addr_value;
    }
    cycleActive = true; //.store(true, std::memory_order_release);

    waitForCycleEnd();
    result = GET_DATA_BUS();
    lastAccessWasWrite = false;
  }else{ // TIA, RIOT or RAM read.
    if(address & 0b10000000 ){
      result = mySystem->m6532().peek(address);
    }else{
      result = mySystem->tia().peek(address);
    }
    setupBusForCartToRead(address, result);
  }
  return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgePort::poke(uInt16 address, uInt8 value)
{
  setupBusForCartToRead(address, value);

  if(! (address & 0x1000) ){ // check if TIA, RIOT or RAM write.
    if(address & 0b10000000 ){
      mySystem->m6532().poke(address, value);
    }else{
      mySystem->tia().poke(address, value);
    }
  }
  return true;
}

void CartridgePort::setupBusForCartToRead(uInt16 address, uInt8 value){
  uInt32 gpio_full_value = (uInt32) (address & MASK_ADDRESS_BUS) | (((uInt32)value)<<13 ) | 0x400000;
  uInt32 gpio_addr_value = (uInt32) (address & MASK_ADDRESS_BUS) | 0x400000;

  LOCK_ADDRESS_BUS();
  if(lastAccessWasWrite){
    waitForCycleEnd();
  }else{
    SET_DATA_BUS_WRITE();
  }

  GPIO_CLR = MASK_ADDRESS_BUS;
  GPIO_SET = gpio_addr_value;

  int g = 15; // ~30ns delay for the latch
  while(--g){
    asm volatile("nop");
  }

  GPIO_CLR = MASK_DATA_BUS;
  GPIO_SET = gpio_full_value;

  cycleActive = true;

  lastAccessWasWrite = true;
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

void CartridgePort::cycleManagerThread() {
  printf("Starting Cycle Manager\n");
  int g;
  cpu_set_t mask;


  // set CPU we want to run on
  CPU_ZERO(&mask);
  CPU_SET(2, &mask);
  int result = sched_setaffinity(0, sizeof(mask), &mask);
  printf("CPU set result CM %d \n", result );

  for(;;){
    if( cycleActive == true) { //.load(std::memory_order_acquire) == true ){
//  printf("Cycle start!\n" );
      g = 560;
      while(--g){
        asm volatile("nop");
      }
      cycleActive = false; //.store(false, std::memory_order_release);
    }
  }
}

void CartridgePort::waitForCycleEnd() // static inline void?
{
/*
  int g = 300;
  while(--g){
    asm volatile("nop");
  }
*/

  while( cycleActive == true ); //.load(std::memory_order_acquire) == true );
}
