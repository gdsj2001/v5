#!/bin/sh
set -eu

home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
evidence_root="${V5_EVIDENCE_ROOT:-$build_root/evidence}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
remote_raw="${V5_REMOTE_UI_RAW:-/tmp/v5_board_ui.raw}"
remote_meta="${V5_REMOTE_UI_META:-/tmp/v5_board_ui.meta}"
local_out="${V5_UI_SCREENSHOT_OUT:-$evidence_root/board_ui/v5_board_ui_${stamp}.png}"
local_raw="${V5_UI_RAW_OUT:-$evidence_root/board_ui/v5_board_ui_${stamp}.raw}"
local_meta="${V5_UI_META_OUT:-$evidence_root/board_ui/v5_board_ui_${stamp}.meta}"
python_bin="${V5_PYTHON_BIN:-}"
apply=0

for arg in "$@"; do
  case "$arg" in
    --apply) apply=1 ;;
    --help)
      echo "usage: capture_v5_board_ui.sh [--apply]"
      echo "dry-run by default; --apply requires V5_BOARD_SSH and captures one board framebuffer frame without sending input"
      exit 0
      ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if [ "$apply" -eq 0 ]; then
  echo "dry-run board UI framebuffer capture:"
  echo "  board:  ${board_ssh:-<set V5_BOARD_SSH>} port=$board_ssh_port"
  echo "  remote raw:  $remote_raw"
  echo "  remote meta: $remote_meta"
  echo "  local png:   $local_out"
  echo "  command: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  echo "  input:  no touch, key, mouse, linuxcncrsh, MDI, start, or motion command is sent"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 3
fi

ssh_base="ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p $board_ssh_port"
scp_base="scp -O -q -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -P $board_ssh_port"

if ! $ssh_base "$board_ssh" 'true' >/dev/null 2>&1; then
  echo "cannot connect to board via ssh: $board_ssh port=$board_ssh_port" >&2
  exit 4
fi

mkdir -p "$(dirname "$local_out")"
$ssh_base "$board_ssh" "RAW='$remote_raw' META='$remote_meta' sh -s" <<'REMOTE_CAPTURE'
set -eu
mode=$(sed -n '1s/^U://;1s/p.*$//;1p' /sys/class/graphics/fb0/modes)
width=${mode%x*}
height=${mode#*x}
bpp=$(cat /sys/class/graphics/fb0/bits_per_pixel)
stride=$(cat /sys/class/graphics/fb0/stride)
test "$bpp" = "24"
test -n "$width"
test -n "$height"
test -n "$stride"
rm -f "$RAW" "$META"
dd if=/dev/fb0 of="$RAW" bs="$stride" count="$height" 2>/tmp/v5_fb_capture_dd.log
printf 'width=%s\nheight=%s\nbpp=%s\nstride=%s\nformat=rgb24\n' "$width" "$height" "$bpp" "$stride" >"$META"
test -s "$RAW"
test -s "$META"
cat "$META"
REMOTE_CAPTURE

$scp_base "$board_ssh:$remote_raw" "$local_raw"
$scp_base "$board_ssh:$remote_meta" "$local_meta"
test -s "$local_raw"
test -s "$local_meta"
if [ -z "$python_bin" ]; then
  if command -v python3 >/dev/null 2>&1 && python3 -c 'import sys' >/dev/null 2>&1; then
    python_bin=python3
  elif command -v python >/dev/null 2>&1 && python -c 'import sys' >/dev/null 2>&1; then
    python_bin=python
  else
    echo "missing usable Python interpreter: set V5_PYTHON_BIN" >&2
    exit 5
  fi
fi
"$python_bin" - "$local_raw" "$local_meta" "$local_out" <<'CONVERT_FB_PY'
import sys
import struct
import zlib
from pathlib import Path
raw_path = Path(sys.argv[1])
meta_path = Path(sys.argv[2])
png_path = Path(sys.argv[3])
meta = {}
for line in meta_path.read_text(encoding='utf-8').splitlines():
    if '=' in line:
        k, v = line.split('=', 1)
        meta[k] = v
width = int(meta['width'])
height = int(meta['height'])
stride = int(meta['stride'])
data = raw_path.read_bytes()
rows = []
for y in range(height):
    row = data[y * stride:y * stride + width * 3]
    if len(row) != width * 3:
        raise SystemExit('short framebuffer row')
    rows.append(b'\x00' + row)

def png_chunk(kind, payload):
    return (struct.pack('>I', len(payload)) + kind + payload +
            struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff))

png_path.parent.mkdir(parents=True, exist_ok=True)
png_path.write_bytes(
    b'\x89PNG\r\n\x1a\n' +
    png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
    png_chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) +
    png_chunk(b'IEND', b'')
)
CONVERT_FB_PY
test -s "$local_out"
rm -f "$local_raw" "$local_meta"
printf 'captured board UI frame: %s\n' "$local_out"
printf 'input: no touch, key, mouse, linuxcncrsh, MDI, start, or motion command was sent\n'
