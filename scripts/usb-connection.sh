#!/bin/bash

INTERFACE="$1"
ACTION="$2"

# Only monitor USB Ethernet.
if [ "$INTERFACE" != "usb0" ]; then
    exit 0
fi

case "$ACTION" in

    up)
        echo "USB connection detected."

        # Wait for the physical connection.
        for i in {1..10}; do

            if [ -f /sys/class/net/usb0/carrier ] &&
               [ "$(cat /sys/class/net/usb0/carrier)" = "1" ]; then

                echo "Starting web server..."
                systemctl start usb-html-server.service
                exit 0
            fi

            sleep 1
        done
        ;;

    down)
        echo "USB disconnected."
        systemctl stop usb-html-server.service
        ;;

esac

exit 0
