savedcmd_/home/hitle/桌面/H3/driver-labs/02-led/led.mod := printf '%s\n'   led.o | awk '!x[$$0]++ { print("/home/hitle/桌面/H3/driver-labs/02-led/"$$0) }' > /home/hitle/桌面/H3/driver-labs/02-led/led.mod
