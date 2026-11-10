#!/bin/bash
# Update a running board: writes the application only (the boot manager stays). See ../README.md.
exec /bin/bash "$(dirname "$0")/_flash.sh" update
