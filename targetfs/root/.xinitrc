#!/bin/sh

wallpaper '#4169E1'
if [ -x /bin/xwmtrace ]; then
	/bin/xwmtrace &
elif [ -x /usr/bin/xwmtrace ]; then
	/usr/bin/xwmtrace &
fi
exec /usr/bin/dwm
