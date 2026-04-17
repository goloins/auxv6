#!/bin/sh

wallpaper '#4169E1'
#if [ -x /bin/xwmselftest ]; then
#	/bin/xwmselftest || echo "xwmselftest: rc=$?"
#elif [ -x /usr/bin/xwmselftest ]; then
#	/usr/bin/xwmselftest || echo "xwmselftest: rc=$?"
#fi
#if [ -x /bin/xwmtrace ]; then
#	/bin/xwmtrace &
#elif [ -x /usr/bin/xwmtrace ]; then
#	/usr/bin/xwmtrace &
#fi
exec /bin/6wm
