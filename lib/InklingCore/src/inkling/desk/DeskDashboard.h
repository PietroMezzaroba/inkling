#pragma once

#include <FreeInkUIDisplayTarget.h>
#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

enum class WeatherCondition : uint8_t {
  Sunny,
  PartlyCloudy,
  Cloudy,
  Rain,
  Storm,
  Snow,
  Fog
};

// Stored in settings.bin as DeskWidgetType (same numbers): only append.
enum class DeskSubWidget : uint8_t {
  StoicQuote = 0,    // was a mock GitHub heatmap
  MoonPhase = 1,
  ObliqueCard = 2,
};

// Everything the desk stand shows besides the clock: real data only.
struct DeskInfo {
  int streakDays = 0;
  int todayPages = 0;
  int goalPages = 15;
  uint32_t totalPages = 0;
  const char* bookTitle = nullptr;  // nullptr: no book on the card
  const char* bookAuthor = nullptr;
  uint8_t bookPct = 0;
  const uint8_t* bookCoverBits = nullptr;
};


class DeskDashboard {
 public:
  DeskDashboard();

  DeskSubWidget activeSubWidget() const { return subWidget_; }
  void setSubWidget(DeskSubWidget widget) { subWidget_ = widget; }
  void cycleSubWidget() {
    subWidget_ = static_cast<DeskSubWidget>((static_cast<uint8_t>(subWidget_) + 1) % 3);
  }

  void render(freeink::ui::DrawTarget& t, const DeviceStatus& status, const DeskInfo& info);

  // Weather icon rendering (32x32 / 48x48 1-bit)
  static void drawWeatherIcon(freeink::ui::DrawTarget& t, int16_t x, int16_t y,
                              WeatherCondition cond, int16_t size = 48);

  // Mechanical split-flap card with central hinge slit, card screws, and crisp digit
  static void drawSplitFlapCard(freeink::ui::DrawTarget& t, int16_t x, int16_t y,
                                int16_t w, int16_t h, char digit);

 private:
  void renderMiniCalendar(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int today);
  void renderMoonPhase(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int day);
  void renderQuote(freeink::ui::DrawTarget& t, freeink::ui::Rect r, bool oblique, int32_t day);

  DeskSubWidget subWidget_ = DeskSubWidget::StoicQuote;
};

}  // namespace inkling
