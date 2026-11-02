#pragma once

#include <cstddef>

// The X3 flash has two app slots (OTA): the Inkling reader in one, the Inkling
// updater in the other (or, before the updater is installed, CrossPet). Both
// escape hatches below work without a USB cable.
//
// The X3's bootloader rolls back: a freshly installed image boots "on
// probation" and, if the chip resets before the image calls markValid(), the
// next boot returns to the previous slot. Each app calls markValid() only
// once it has proven itself (screen drawn), so a crash in a new build lands
// back in the other app instead of a crash loop.
namespace firmware {

// Confirms this image works and cancels the pending rollback. Cheap and
// harmless when the image is already valid.
void markValid();

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
