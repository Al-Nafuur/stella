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

#ifndef CARTRIDGEPORT_WIDGET_HXX
#define CARTRIDGEPORT_WIDGET_HXX

class CartridgePort;
class PopUpWidget;

#include "CartEnhancedWidget.hxx"

class CartridgePortWidget : public CartridgeEnhancedWidget
{
  public:
    CartridgePortWidget(GuiObject* boss, const GUI::Font& lfont,
                      const GUI::Font& nfont,
                      int x, int y, int w, int h,
                      CartridgePort& cart);
    ~CartridgePortWidget() override = default;

  private:
    string manufacturer() override { return "Al Nafuur"; }

    string description() override;

  private:
    // Following constructors and assignment operators not supported
    CartridgePortWidget() = delete;
    CartridgePortWidget(const CartridgePortWidget&) = delete;
    CartridgePortWidget(CartridgePortWidget&&) = delete;
    CartridgePortWidget& operator=(const CartridgePortWidget&) = delete;
    CartridgePortWidget& operator=(CartridgePortWidget&&) = delete;
};

#endif
