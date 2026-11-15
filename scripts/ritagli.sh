#!/bin/sh
# Copies the clippings saved on the X3 (/Ritagli/*.md) to the Mac.
#
#   scripts/ritagli.sh [destination-folder]
#
# The device must be in Wi-Fi mode ("Wi-Fi: giornale e libri" in the library).
# The reader keeps the list of its clipping files in /Ritagli/indice.txt,
# because the portal serves files but does not list folders. Files are
# overwritten: each one is a whole book's clippings, appended to on the device.
# INKLING_HOST overrides the address (default inkling.local).
set -eu

HOST="${INKLING_HOST:-inkling.local}"
DEST="${1:-$HOME/second-brain/00-Inbox/Ritagli X3}"

urlencode() {
  python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$1"
}

LIST="$(curl -fsS --max-time 20 "http://$HOST/file?path=/Ritagli/indice.txt")" || {
  echo "nessun ritaglio sul dispositivo (o dispositivo non in Wi-Fi)" >&2
  exit 1
}

mkdir -p "$DEST"
echo "$LIST" | sort -u | while IFS= read -r path; do
  [ -n "$path" ] || continue
  name="$(basename "$path")"
  if curl -fsS --max-time 60 -o "$DEST/$name" "http://$HOST/file?path=$(urlencode "$path")"; then
    echo "copiato: $name"
  else
    echo "non letto: $path" >&2
  fi
done
