
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

#ifndef CARTRIDGEPORT_HXX
#define CARTRIDGEPORT_HXX

#include <thread>
#include <atomic>

#include "bspf.hxx"
#include "CartEnhanced.hxx"
//#include "Cart.hxx"
#ifdef DEBUGGER_SUPPORT
  #include "CartPortWidget.hxx"
#endif

/**
  This is the standard Atari cartridge port.

  @author  Wolfgang Stubig
*/
class CartridgePort : public CartridgeEnhanced
{
  friend class CartridgePortWidget;

  public:
    /**
      Create a new cartridge using the specified image

      @param image     Pointer to the ROM image
      @param size      The size of the ROM image
      @param md5       The md5sum of the ROM image
      @param settings  A reference to the various settings (read-only)
      @param bsSize    The size specified by the bankswitching scheme
    */
    CartridgePort(const ByteBuffer& image, size_t size, string_view md5,
                const Settings& settings, size_t bsSize = 4_KB);
    ~CartridgePort() override = default;

  public:
    /**
      Reset device to its power-on state
    */
    void reset() override;

    /**
      Get a descriptor for the device name (used in error checking).

      @return The name of the object
    */
    string name() const override { return "CartridgePort"; }

    /**
      Install cartridge in the specified system.  Invoked by the system
      when the cartridge is attached to it.

      @param system The system the device should install itself in
    */

    void install(System& system) override;

  #ifdef DEBUGGER_SUPPORT
    /**
      Get debugger widget responsible for accessing the inner workings
      of the cart.
    */
    CartDebugWidget* debugWidget(GuiObject* boss, const GUI::Font& lfont,
        const GUI::Font& nfont, int x, int y, int w, int h) override
    {
      return new CartridgePortWidget(boss, lfont, nfont, x, y, w, h, *this);
    }
  #endif

  public:
    /**
      Get the byte at the specified address.

      @return The byte at the specified address
    */
    uInt8 peek(uInt16 address) override;

    /**
      Change the byte at the specified address to the given value

      @param address The address where the value should be stored
      @param value The value to be stored at the address
      @return  True if the poke changed the device address space, else false
    */
    bool poke(uInt16 address, uInt8 value) override;

    /**
      Patch the cartridge ROM.

      @param address  The ROM address to patch
      @param value    The value to place into the address
      @return    Success or failure of the patch operation
    */
    bool patch(uInt16 address, uInt8 value) override;

    /**
      Save the current state of this cart to the given Serializer.

      @param out  The Serializer object to use
      @return  False on any errors, else true
    */
    bool save(Serializer& out) const override;

    /**
      Load the current state of this cart from the given Serializer.

      @param in  The Serializer object to use
      @return  False on any errors, else true
    */
    bool load(Serializer& in) override;

  private:
    bool checkSwitchBank(uInt16, uInt8) override { return false; }
    void waitForCycleEnd();
    void cycleManagerThread();

  private:
    bool lastAccessWasWrite;
    int  mem_fd;
    uInt16 lastAddress{0x0000};
    void *gpio_map;

    std::thread myCycleTimerThread;
    std::atomic<bool> cycleActive{false};

    // I/O access
    volatile unsigned *gpio;

  private:
    // Following constructors and assignment operators not supported
    CartridgePort() = delete;
    CartridgePort(const CartridgePort&) = delete;
    CartridgePort(CartridgePort&&) = delete;
    CartridgePort& operator=(const CartridgePort&) = delete;
    CartridgePort& operator=(CartridgePort&&) = delete;
};

#endif
