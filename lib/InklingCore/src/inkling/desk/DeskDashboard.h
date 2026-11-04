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

enum class DeskSubWidget : uint8_t {
  GitHubHeatmap,
  MoonPhase,
  DailyQuote
};

struct WeatherData {
  WeatherCondition condition = WeatherCondition::Sunny;
  int8_t tempC = 18;
  int8_t tempMin = 11;
  int8_t tempMax = 20;
  uint8_t humidity = 58;
  const char* conditionText = "Sereno · Ottima visibilità";
  const char* location = "Milano · 122m";
  const char* syncTime = "18:00 (Wi-Fi Off)";
};

class DeskDashboard {
 public:
  DeskDashboard();

  DeskSubWidget activeSubWidget() const { return subWidget_; }
  void setSubWidget(DeskSubWidget widget) { subWidget_ = widget; }
  void cycleSubWidget() {
    subWidget_ = static_cast<DeskSubWidget>((static_cast<uint8_t>(subWidget_) + 1) % 3);
  }

  void render(freeink::ui::DrawTarget& t, const DeviceStatus& status, int readingStreak,
              int todayPages, int goalPages, int totalPages);

  // Weather icon rendering (32x32 / 48x48 1-bit)
  static void drawWeatherIcon(freeink::ui::DrawTarget& t, int16_t x, int16_t y,
                              WeatherCondition cond, int16_t size = 48);

  // Mechanical split-flap card with central hinge slit, card screws, and crisp digit
  static void drawSplitFlapCard(freeink::ui::DrawTarget& t, int16_t x, int16_t y,
                                int16_t w, int16_t h, char digit);

 private:
  void renderMiniCalendar(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int today);
  void renderGitHubHeatmap(freeink::ui::DrawTarget& t, freeink::ui::Rect r);
  void renderMoonPhase(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int day);
  void renderDailyQuote(freeink::ui::DrawTarget& t, freeink::ui::Rect r);

  DeskSubWidget subWidget_ = DeskSubWidget::GitHubHeatmap;
  WeatherData weather_;
};

}  // namespace inkling
