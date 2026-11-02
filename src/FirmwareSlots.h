#pragma once

#include <cstddef>

// The X3 flash has two app slots (OTA). Inkling runs from one; the other keeps
// whatever was installed before (e.g. CrossPet). Both escape hatches below work
// without a USB cable.
namespace firmware {

// Name of the app in the other slot ("" if it holds no valid image).
const char* otherSlotName();

// Makes the other slot the boot slot and restarts into it. Returns only on
// failure (no valid image there).
bool bootOtherSlot();

// SD self-update: if `path` exists on the card, flash it into the other slot,
// rename it to "<path>.installed" and restart. Returns only when there is no
// update file or flashing failed; `error` then holds a short reason ("" when
// there was simply nothing to install).
constexpr const char* kSdUpdatePath = "/inkling.bin";
bool installFromSd(const char* path, const char** error);

}  // namespace firmware
