#!/bin/bash
echo "Library Information:"
echo "===================="
file "$1"
echo ""
echo "Architecture:"
lipo -info "$1" 2>/dev/null || echo "Single architecture"
echo ""
echo "Dependencies:"
otool -L "$1"
echo ""
echo "Minimum macOS version:"
otool -l "$1" | grep -A 4 "LC_BUILD_VERSION" | grep "minos" | awk '{print "macOS " $2}'
echo ""
echo "Library size:"
ls -lh "$1" | awk '{print $5}'
