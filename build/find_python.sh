#!/bin/sh

# Required version of Python
# Python 2.6 = 0x2060000
# Python 3.0 = 0x3000000
VERSION=${1:-0x2060000}

for pypath in "$PYTHON" "$PYTHON2" "$PYTHON3" python python2 python3; do
  if [ "x$pypath" != "x" ]; then
    DETECT_PYTHON="import sys;sys.exit((sys.hexversion < $VERSION) and 1 or 0)"
    if "$pypath" -c "$DETECT_PYTHON" >/dev/null 2>/dev/null; then
      echo $pypath
      exit 0
    fi
  fi
done
exit 1
