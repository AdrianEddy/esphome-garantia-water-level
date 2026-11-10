#!/bin/bash
# Lists the DFU devices dfu-util can see (U3 in DFU shows as [0483:df11]).
exec /bin/bash "$(dirname "$0")/_flash.sh" list
