#!/bin/sh
# Build the real plugin sources + bundled SDK against the synthetic engine
# with ASan/UBSan, then run the ownership regressions (macOS arm64/x86_64).
#   tests/ownership/run.sh [SOURCE_ROOT] [expected-failure ...]
# SOURCE_ROOT defaults to this checkout; pass another tree (e.g. a git archive
# of an older commit) to run this same harness against its sources.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:-$here/../..}" && pwd)
[ $# -gt 0 ] && shift
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
api="$root/4D Plugin API"
a="$root/a"
case "$(uname -m)" in
    arm64) cv="$root/include/arm/opencv4"; hal="$a/libcarotene_objs.a" ;;
    *) cv="$root/include/intel/opencv4"; hal="$a/libippiw.a $a/libippicv.a" ;;
esac
flags="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-objc-arc -Wno-deprecated-declarations -Wno-multichar -w"
build() { clang++ $flags -I"$api" -I"$api/Classes" -I"$root/support" -I"$root" -I"$cv" -I"$root/include" -I"$here" -std=c++11 -c "$@"; }
build -x objective-c++ "$api/4DPluginAPI.c" -o "$out/api.o"
build -x objective-c++ "$api/Classes/C_TEXT.cpp" -o "$out/ctext.o"
build -x objective-c++ "$root/support/4DPlugin-JSON.cpp" -o "$out/json.o"
build -x c++ "$here/fake_engine.cpp" -o "$out/engine.o"
build -x objective-c++ "$here/harness.mm" -o "$out/harness.o" # includes 4DPlugin-opencv-qr.cpp
clang++ $flags "$out"/*.o -o "$out/harness" \
    "$a/libopencv_objdetect.a" "$a/libopencv_calib3d.a" "$a/libopencv_features2d.a" "$a/libopencv_flann.a" \
    "$a/libopencv_dnn.a" "$a/libopencv_imgcodecs.a" "$a/libopencv_imgproc.a" "$a/libopencv_core.a" \
    "$a/libquirc.a" "$a/liblibpng.a" "$a/liblibtiff.a" "$a/libjpeg.a" "$a/liblibwebp.a" "$a/liblibopenjp2.a" \
    "$a/libOpenEXRUtil-3_1.30.5.1.a" "$a/libOpenEXR-3_1.30.5.1.a" "$a/libOpenEXRCore-3_1.30.5.1.a" \
    "$a/libIlmThread-3_1.30.5.1.a" "$a/libIex-3_1.30.5.1.a" "$a/libImath-3_1.29.4.0.a" "$a/libzlib.a" \
    $hal "$a/libittnotify.a" "$a/libtbb.a" \
    -framework Cocoa -framework Accelerate -framework OpenCL -framework ImageIO -framework CoreGraphics
ASAN_OPTIONS=abort_on_error=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 "$out/harness" "$@"
