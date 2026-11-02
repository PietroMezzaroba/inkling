// Inkling rescue (Xteink X3): a one-shot diagnostic app built like phase 0
// (stock Arduino libraries, nothing new at boot). It copies the evidence of
// the last crash to the SD card, then returns to the firmware in the other
// slot (CrossPet):
//   /.inkling/coredump.bin   raw "coredump" partition (ESP-IDF ELF core dump)
//   /.inkling/otadata.bin    raw "otadata" partition (boot slot selection)
//   /.inkling/rescue.txt     slots, image states, reset reason
// The core dump is decoded on the Mac with the ELF of the build that crashed.

#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <FreeInkUIDisplayTarget.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <cstdio>

#include "inkling/Screens.h"

// InklingCore's log hook (see src/common/CoreLogHook.h): nothing to log here.
extern "C" void inkling_core_log(const char*, ...) {}

namespace {

EInkDisplay* display = nullptr;
inkling::UiApp* ui = nullptr;
char message[96];

void show(const char* text) {
  snprintf(message, sizeof(message), "%s", text);
  ui->setScreen(inkling::messageScreen, message, freeink::ui::RefreshHint::Full);
  ui->render(freeink::ui::InputSnapshot{});
  freeink::ui::present(*display, ui->lastRenderRefreshHint());
}

bool copyPartition(esp_partition_type_t type, esp_partition_subtype_t subtype, const char* path) {
  const esp_partition_t* p = esp_partition_find_first(type, subtype, nullptr);
  if (!p) return false;
  FsFile f = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  static uint8_t buf[4096];
  for (uint32_t off = 0; off < p->size; off += sizeof(buf)) {
    const uint32_t n = p->size - off < sizeof(buf) ? p->size - off : sizeof(buf);
    if (esp_partition_read(p, off, buf, n) != ESP_OK) return false;
    if (f.write(buf, n) != n) return false;
  }
  f.close();
  return true;
}

const char* stateName(const esp_partition_t* p) {
  esp_ota_img_states_t s;
  if (!p || esp_ota_get_state_partition(p, &s) != ESP_OK) return "unknown";
  switch (s) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending-verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
  }
}

void describe(FsFile& f, const char* label, const esp_partition_t* p) {
  char line[200];
  esp_app_desc_t d{};
  const bool ok = p && esp_ota_get_partition_description(p, &d) == ESP_OK;
  snprintf(line, sizeof(line), "%s: %s state=%s app=%s version=%s built=%s %s\n", label, p ? p->label : "-",
           stateName(p), ok ? d.project_name : "-", ok ? d.version : "-", ok ? d.date : "-", ok ? d.time : "-");
  f.write(reinterpret_cast<const uint8_t*>(line), strlen(line));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  freeink::selectXteinkDevice();

  const auto& pins = BoardConfig::ACTIVE.display;
  SPI.begin(pins.sclk, BoardConfig::ACTIVE.sd.miso, pins.mosi, pins.cs);
  const bool sdReady = SdMan.begin();

  static EInkDisplay einkDisplay(pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy);
  display = &einkDisplay;
  display->setDisplayX3();
  display->begin();
  static freeink::ui::DisplayTarget target(display->getFrameBuffer(), display->getDisplayWidth(),
                                           display->getDisplayHeight(), display->getDisplayWidthBytes());
  static inkling::UiApp app(target, target.deviceContext());
  ui = &app;
  ui->setClearColor(freeink::ui::Color::White);

  if (!sdReady) {
    show("Soccorso: scheda SD assente");
  } else {
    show("Soccorso: salvo la diagnostica...");
    SdMan.mkdir("/.inkling", true);
    const bool core = copyPartition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                    "/.inkling/coredump.bin");
    const bool ota = copyPartition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, "/.inkling/otadata.bin");

    FsFile f = SdMan.open("/.inkling/rescue.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (f) {
      char line[160];
      snprintf(line, sizeof(line), "reset_reason=%d coredump_copied=%d otadata_copied=%d core_image_check=%d\n",
               static_cast<int>(esp_reset_reason()), core, ota, static_cast<int>(esp_core_dump_image_check()));
      f.write(reinterpret_cast<const uint8_t*>(line), strlen(line));
      describe(f, "running", esp_ota_get_running_partition());
      describe(f, "other", esp_ota_get_next_update_partition(nullptr));
      describe(f, "boot", esp_ota_get_boot_partition());
      f.close();
    }
    show(core ? "Diagnostica salvata. Torno a CrossPet..." : "Coredump non letto. Torno a CrossPet...");
  }

  delay(4000);
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other && esp_ota_set_boot_partition(other) == ESP_OK) esp_restart();
  show("Nessun firmware nell'altro slot");
}

void loop() { delay(1000); }
