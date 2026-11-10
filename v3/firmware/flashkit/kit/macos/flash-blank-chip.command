#!/bin/bash
# New (blank) board only: writes the boot manager and the application. See ../README.md.
exec /bin/bash "$(dirname "$0")/_flash.sh" blank
