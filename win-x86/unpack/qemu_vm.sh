#!/bin/sh
# Launch a QEMU i386 VM for the VMProtect seed capture.
#
# Usage:
#   # First time: install Windows from an ISO
#   ./qemu_vm.sh create <windows.iso>
#
#   # Boot the VM for a capture (GDB stub on port 1234)
#   ./qemu_vm.sh run
#
#   # Boot without GDB (for setup, copying files in, etc.)
#   ./qemu_vm.sh setup
#
# The VM disk image lives at $QEMU_DISK (default: ~/qemu-lp-capture/win32.qcow2).
# Share files with the guest via $QEMU_SHARED (default: ~/qemu-lp-capture/shared/).
#
# QEMU runs with -accel tcg (software emulation, not Apple Hypervisor Framework).
# TCG is CPU-identical to real x86 — VMProtect cannot distinguish it from hardware.
#
# The CPU must be -cpu max: on the default qemu32 Windows 10 never gets past its boot manager
# and the screen looks like a broken VGA (qemu32 lacks NX, the likely reason). hypervisor=off
# hides the CPUID hypervisor bit that max would otherwise show to VMProtect.
#
# create and setup run two threaded vCPUs for speed ($QEMU_SMP); run stays on one, so the
# capture's watchpoint sees a single CPU. The guest network reaches the host only.
#
# Display: VNC on port 5901 (QEMU_VNC=:N for port 5900+N). Connect with Screen Sharing.
set -eu

cmd=${1:-run}
iso=${2:-}
disk=${QEMU_DISK:-$HOME/qemu-lp-capture/win32.qcow2}
shared=${QEMU_SHARED:-$HOME/qemu-lp-capture/shared}
mem=${QEMU_MEM:-2G}
smp=${QEMU_SMP:-2}
vnc_display=${QEMU_VNC:-:1}
vnc_port=$((5900 + ${vnc_display#:}))
vnc_pass=${QEMU_VNC_PASS:-qemu}
monitor_sock="${disk%/*}/monitor.sock"

mkdir -p "$(dirname "$disk")" "$shared"

common="-m $mem -cpu max,hypervisor=off -display none -vnc 127.0.0.1${vnc_display},password=on \
  -monitor unix:${monitor_sock},server,nowait \
  -drive file=$disk,format=qcow2 \
  -device rtl8139,netdev=net0 -netdev user,id=net0,restrict=on \
  -vga std -usb -device usb-tablet ${QEMU_EXTRA:-}"
fast="-accel tcg,thread=multi -smp $smp"
shared_drive="-drive file=fat:rw:$shared,format=raw,media=disk"

start_qemu() {
  # start QEMU in background, set VNC password via monitor, then wait
  # shellcheck disable=SC2086
  qemu-system-i386 $common "$@" &
  qemu_pid=$!
  sleep 1
  # set the VNC password so Screen Sharing can connect
  if [ -S "$monitor_sock" ]; then
    python3 -c "
import socket, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('$monitor_sock')
time.sleep(0.2)
s.recv(4096)
s.sendall(b'set_password vnc $vnc_pass\n')
time.sleep(0.2)
s.recv(4096)
s.close()
" 2>/dev/null || true
  fi
  echo ""
  echo "  Connect: open vnc://localhost:$vnc_port  (password: $vnc_pass)"
  echo ""
  wait $qemu_pid
}

case "$cmd" in
  create)
    if [ -z "$iso" ]; then
      echo "Usage: $0 create <windows.iso>"
      echo "  Downloads: any 32-bit Windows (7/10) ISO works"
      exit 1
    fi
    if [ -f "$disk" ]; then
      echo "Disk already exists: $disk"
      echo "  Remove it first if you want to start over"
      exit 1
    fi
    echo "Creating 20 GB disk at $disk"
    qemu-img create -f qcow2 "$disk" 20G
    echo "Booting from ISO: $iso"
    echo "  Install Windows, then shut down the VM"
    start_qemu $fast -cdrom "$iso" -boot once=d
    ;;

  setup)
    if [ ! -f "$disk" ]; then
      echo "No disk at $disk — run '$0 create <iso>' first"
      exit 1
    fi
    echo "Booting VM (no GDB stub) — copy the exe into $shared/"
    echo "  The shared folder appears as a second drive in the guest"
    start_qemu $fast $shared_drive
    ;;

  run)
    if [ ! -f "$disk" ]; then
      echo "No disk at $disk — run '$0 create <iso>' first"
      exit 1
    fi
    echo "Booting VM with GDB stub on tcp::1234"
    echo ""
    echo "  In another terminal:"
    echo "    python3 -I win-x86/unpack/qemu_capture.py /path/to/Rocksmith2014.exe capture.bin"
    echo ""
    echo "  The VM stays paused until that connects. The steps are at the top of qemu_capture.py."
    start_qemu -accel tcg $shared_drive -gdb tcp:127.0.0.1:1234 -S
    ;;

  *)
    echo "Usage: $0 {create <iso>|setup|run}"
    exit 1
    ;;
esac
