#!/bin/bash
# Build Trajet and open it.
#   ./build.sh          build (Release) and launch
#   ./build.sh debug    build a Debug version and launch
#   ./build.sh xcode    generate an Xcode project in build-xcode/ and open it
set -e
cd "$(dirname "$0")"

if [ "$1" = "xcode" ]; then
    cmake -B build-xcode -G Xcode
    open build-xcode/Trajet.xcodeproj
    exit 0
fi

CONFIG=Release
[ "$1" = "debug" ] && CONFIG=Debug

cmake -B build -DCMAKE_BUILD_TYPE=$CONFIG
cmake --build build --config $CONFIG -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

APP="build/Trajet_artefacts/$CONFIG/Trajet.app"
if [ -d "$APP" ]; then
    echo "Built: $APP"
    open "$APP"
fi
