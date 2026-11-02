#!/bin/sh
# Builds the firmware and sends it to the X3 over Wi-Fi.
#
#   scripts/ota.sh            build + upload + show device info
#   scripts/ota.sh --log      only print the device log
#   scripts/ota.sh --info     only print device info
#
# The device must be in "Aggiornamento Wi-Fi" mode (library menu). The token
# lives in .ota-token (git-ignored) and must match token= in /.inkling/wifi.txt
# on the card. INKLING_HOST overrides the address (default inkling.local).
set -eu

cd "$(dirname "$0")/.."
HOST="${INKLING_HOST:-inkling.local}"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"

case "${1:-}" in
  --log) exec curl -fsS "http://$HOST/log" ;;
  --info) exec curl -fsS "http://$HOST/info" ;;
esac

if [ ! -s .ota-token ]; then
  echo "missing .ota-token (see README)" >&2
  exit 1
fi
TOKEN="$(cat .ota-token)"

"$PIO" run -e x3
BIN=.pio/build/x3/firmware.bin

echo "uploading $(wc -c < "$BIN" | tr -d ' ') bytes to $HOST ..."
curl -fsS --max-time 300 -X POST \
  -H "X-Inkling-Token: $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$BIN" \
  "http://$HOST/update"

echo "waiting for the device to restart ..."
sleep 8
echo "done. The device restarts into the new firmware (normal mode, Wi-Fi off)."
