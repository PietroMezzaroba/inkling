#!/bin/sh
# Builds the firmware and sends it to the X3 over Wi-Fi.
#
#   scripts/ota.sh            build + upload the reader
#   scripts/ota.sh --updater  build + upload the updater itself (it lands in the
#                             reader's slot; the two slots swap roles)
#   scripts/ota.sh --info     only print device info
#   scripts/ota.sh --log      only print the updater's log
#   scripts/ota.sh --sdlog    only print the reader's boot log from the card
#
# The device must be running the updater ("Aggiornamento Wi-Fi" in the
# library, which restarts into the other app slot). The token
# lives in .ota-token (git-ignored) and must match token= in /.inkling/wifi.txt
# on the card. INKLING_HOST overrides the address (default inkling.local).
set -eu

cd "$(dirname "$0")/.."
HOST="${INKLING_HOST:-inkling.local}"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"

case "${1:-}" in
  --log) exec curl -fsS "http://$HOST/log" ;;
  --sdlog) exec curl -fsS "http://$HOST/sdlog?name=boot" ;;
  --info) exec curl -fsS "http://$HOST/info" ;;
  --coredump) exec curl -fsS -o "${2:-coredump.bin}" "http://$HOST/coredump" ;;
esac
ENV=x3
[ "${1:-}" = "--updater" ] && ENV=x3-updater

if [ ! -s .ota-token ]; then
  echo "missing .ota-token (see README)" >&2
  exit 1
fi
TOKEN="$(cat .ota-token)"

"$PIO" run -e "$ENV"
BIN=.pio/build/$ENV/firmware.bin

echo "uploading $(wc -c < "$BIN" | tr -d ' ') bytes to $HOST ..."
curl -fsS --max-time 300 -X POST \
  -H "X-Inkling-Token: $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$BIN" \
  "http://$HOST/update"

echo "waiting for the device to restart ..."
sleep 8
echo "done: the device restarts into the new reader. If it crashes before"
echo "showing the library it rolls back to the updater: scripts/ota.sh --sdlog"
