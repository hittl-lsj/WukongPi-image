#!/bin/sh

# 只在交互式终端显示，避免影响 scp 等非交互命令。
case "$-" in
    *i*) ;;
    *) return ;;
esac

IP_ADDR="$(ip -4 addr show dev eth0 2>/dev/null |
    awk '/inet / { print $2; exit }')"
[ -n "$IP_ADDR" ] || IP_ADDR="not configured"

MEM_INFO="$(free -m | awk '/^Mem:/ {
    printf "%s MiB used / %s MiB total", $3, $2
}')"

if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
    TEMP_RAW="$(cat /sys/class/thermal/thermal_zone0/temp)"
    TEMP_INFO="$(awk -v temp="$TEMP_RAW" \
        'BEGIN { printf "%.1f C", temp / 1000 }')"
else
    TEMP_INFO="unavailable"
fi

printf '\033[1;33m'
cat <<'EOF'
 __        __     _  __                   ____  _
 \ \      / /   _| |/ /___  _ __   __ _ |  _ \(_)
  \ \ /\ / / | | | ' // _ \| '_ \ / _` || |_) | |
   \ V  V /| |_| | . \ (_) | | | | (_| ||  __/| |
    \_/\_/  \__,_|_|\_\___/|_| |_|\__, ||_|   |_|
                                    |___/
EOF
printf '\033[0m'

printf '  Board    : WuKong Pi H3\n'
printf '  Hostname : %s\n' "$(hostname)"
printf '  Kernel   : %s\n' "$(uname -r)"
printf '  Memory   : %s\n' "$MEM_INFO"
printf '  CPU temp : %s\n' "$TEMP_INFO"
printf '  Ethernet : %s\n\n' "$IP_ADDR"

unset IP_ADDR MEM_INFO TEMP_RAW TEMP_INFO
