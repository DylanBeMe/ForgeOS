#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"
HOME_DIR=${FORGESHELL_HOME:-/mnt/forgeshell}
STATE_DIR=$HOME_DIR/state
main=$(forge_main_mount 2>/dev/null || true)
[ -n "$main" ] || main=/mnt
outdir=$main/forgeos-test-reports
mkdir -p "$outdir" || { forge_dialog "Performance Snapshot" "Unable to create $outdir"; exit 1; }
stamp=$(date +%Y%m%d-%H%M%S 2>/dev/null || printf current)
report=$outdir/q90-performance-$stamp.txt
parent=${FORGESHELL_PARENT_PID:-$PPID}
cache=$STATE_DIR/library-cache.tsv
uptime_cs() {
  awk '{split($1,p,"."); f=(p[2] "00"); printf "%d", (p[1]*100)+substr(f,1,2)}' /proc/uptime 2>/dev/null
}
start=$(uptime_cs)
reads=0
if [ -r "$cache" ]; then
  while [ "$reads" -lt 20 ]; do cat "$cache" >/dev/null || break; reads=$((reads + 1)); done
fi
end=$(uptime_cs)
{
  printf 'ForgeOS Q90 Performance Snapshot\n'
  printf 'Generated: %s\n\n' "$(date 2>/dev/null || printf unknown)"
  printf '[ForgeShell process]\n'
  if [ -r "/proc/$parent/status" ]; then
    sed -n '/^Name:/p;/^State:/p;/^VmSize:/p;/^VmRSS:/p;/^VmData:/p;/^VmStk:/p;/^Threads:/p' "/proc/$parent/status"
  else
    printf 'ForgeShell process metrics unavailable (parent PID %s).\n' "$parent"
  fi
  printf '\n[System load]\n'
  cat /proc/loadavg 2>/dev/null || true
  printf 'CPU frequency: '
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || printf 'unavailable\n'
  printf '\n[Library state]\n'
  if [ -r "$cache" ]; then
    games=$(awk 'BEGIN{n=0} $0 !~ /^#/ && NF {n++} END{print n}' "$cache" 2>/dev/null || printf 0)
    bytes=$(wc -c < "$cache" 2>/dev/null || printf 0)
    printf 'Cached games: %s\nCache bytes: %s\n' "$games" "$bytes"
    if forge_numeric "$start" && forge_numeric "$end"; then
      printf 'Twenty cache reads: %s centiseconds (%s completed)\n' "$((end-start))" "$reads"
    fi
  else
    printf 'No library cache exists yet.\n'
  fi
  printf '\n[Recorded timings]\n'
  for metrics in state/startup-metrics.ini state/scan-metrics.ini; do
    if [ -r "$HOME_DIR/$metrics" ]; then
      printf '%s\n' "$metrics"
      sed 's/^/  /' "$HOME_DIR/$metrics"
    fi
  done
  printf '\n[State file sizes]\n'
  for file in config.ini config.ini.last-good state/favorites.txt state/sessions.log \
              state/game-overrides.tsv state/startup-metrics.ini state/scan-metrics.ini \
              library/metadata.tsv; do
    if [ -f "$HOME_DIR/$file" ]; then
      size=$(wc -c < "$HOME_DIR/$file" 2>/dev/null || printf 0)
      printf '%s=%s bytes\n' "$file" "$size"
    fi
  done
  printf '\n[Interpretation]\n'
  printf 'Target idle VmRSS: under 8192 kB.\n'
  printf 'Normal navigation should not grow state files or write continuously.\n'
  printf 'Run this snapshot before and after a long browsing session for comparison.\n'
} > "$report"
forge_dialog "Performance Snapshot" "Report saved to:\n\n$report\n\nCompare snapshots before and after testing."
