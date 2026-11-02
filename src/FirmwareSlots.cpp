#include "FirmwareSlots.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include <cstdio>

namespace firmware {

namespace {

constexpr uint8_t kEspImageMagic = 0xE9;

const esp_partition_t* otherSlot() { return esp_ota_get_next_update_partition(nullptr); }

}  // namespace

const char* otherSlotName() {
  static char name[48];
  esp_app_desc_t desc;
  const esp_partition_t* slot = otherSlot();
  if (!slot || esp_ota_get_partition_description(slot, &desc) != ESP_OK) return "";
  snprintf(name, sizeof(name), "%s %s", desc.project_name, desc.version);
  return name;
}

bool bootOtherSlot() {
  const esp_partition_t* slot = otherSlot();
  esp_app_desc_t desc;
  if (!slot || esp_ota_get_partition_description(slot, &desc) != ESP_OK) return false;
  // set_boot_partition verifies the image before switching.
  if (esp_ota_set_boot_partition(slot) != ESP_OK) return false;
  esp_restart();
}

bool installFromSd(const char* path, const char** error) {
  *error = "";
  if (!SdMan.ready() || !SdMan.exists(path)) return false;

  FsFile file = SdMan.open(path, O_RDONLY);
  if (!file) {
    *error = "file illeggibile";
    return false;
  }
  const size_t size = file.fileSize();
  uint8_t magic = 0;
  if (size == 0 || file.read(&magic, 1) != 1 || magic != kEspImageMagic) {
    *error = "non e' un firmware ESP32";
    return false;
  }
  file.seekSet(0);

  if (!Update.begin(size)) {
    *error = "file troppo grande";
    return false;
  }
  static uint8_t buf[4096];
  size_t done = 0;
  while (done < size) {
    const int n = file.read(buf, sizeof(buf));
    if (n <= 0 || Update.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      Update.abort();
      *error = "errore di lettura o scrittura";
      return false;
    }
    done += static_cast<size_t>(n);
  }
  file.close();

  // end() verifies checksum and SHA-256, then makes the new slot the boot slot.
  if (!Update.end(true)) {
    *error = "firmware corrotto";
    return false;
  }

  char installed[64];
  snprintf(installed, sizeof(installed), "%s.installed", path);
  SdMan.remove(installed);
  SdMan.rename(path, installed);
  esp_restart();
}

}  // namespace firmware
