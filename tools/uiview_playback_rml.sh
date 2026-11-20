#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "${REPO_ROOT}/output/uiview"

if [[ ! -f /.dockerenv ]]; then
    # Forward the render-size override so 4K reproduction works from the host.
    docker compose run --rm -e EVO_UIVIEW_SIZE="${EVO_UIVIEW_SIZE:-}" ps5-dev bash ./tools/uiview_playback_rml.sh
    exit $?
fi

cd "${REPO_ROOT}"

# #60: regenerate the embedded RmlUi asset bundle before compiling, so the
# host preview always renders whatever assets/{rml,fonts,icons} currently
# contains - see tools/bundle_rml_assets.py.
python3 tools/bundle_rml_assets.py

# #68: ui_rml/src/rmlui_patch/GeometryBackgroundBorder.cpp is RmlUi's corner
# tessellation with a finer GetNumPoints(); compiled into the binary it preempts
# librmlui.so's copies of that TU. See rmlui_patch/VENDORED.md.
#
# The harness renders through NoStream's CPU rasteriser. It used to also link
# RmlUi's GL3 interface for a plane compare against the console's OpenGL path;
# that path is gone (the console is bare-metal AGC only), so the compare had
# nothing left to compare against and went with it.
# -----------------------------------------------------------------------------
echo "--- building uiview_playback_rml"
mkdir -p output/uiview/obj
# One g++ over every source took ~95 s, serial. Compile each file on its own, all at
# once, through ccache (the container's /ccache volume, same as package-app.sh), then
# link: an unchanged file costs nothing on the next run.
CXX_CACHE=""
if [[ "${EVO_NO_CCACHE:-}" != "1" ]] && command -v ccache >/dev/null 2>&1; then CXX_CACHE="ccache"; fi
CXX_SRCS=(
    tools/uiview_playback_rml.cpp
    projects/evoplayer/ui_rml/src/rmlui_patch/GeometryBackgroundBorder.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_render.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_system.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_app.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_bridge.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_fileinterface.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_bundle.cpp
    projects/evoplayer/ui_rml/src/evo_rmlui_bundle_data.cpp
)
C_OBJS=()
for c in projects/evoplayer/src/evo_data_path.c projects/evoplayer/src/evo_error.c; do
    o="output/uiview/obj/$(basename "${c%.c}").o"
    gcc -O2 -std=c11 -D_DEFAULT_SOURCE -Iprojects/evoplayer/include -c "$c" -o "$o"
    C_OBJS+=("$o")
done
CXX_OBJS=()
CXX_PIDS=()
for c in "${CXX_SRCS[@]}"; do
    o="output/uiview/obj/cxx_$(basename "${c%.cpp}").o"
    CXX_OBJS+=("$o")
    ${CXX_CACHE} g++ -O2 -std=c++17 \
        -Iprojects/evoplayer \
        -Iprojects/evoplayer/ui_rml/include \
        -Iprojects/evoplayer/ui/include \
        -Iprojects/evoplayer/ui_rml/src \
        -Iprojects/evoplayer/pp/include \
        -Iprojects/evoplayer/include \
        -Ibuild/rmlui-host/RmlUi/Include \
        -Ibuild/rmlui-host-dist/include/freetype2 \
        -Iprojects/evoplayer/addons/include \
        -Iprojects/evoplayer/core/include \
        -Iprojects/evoplayer/media/include \
        -c "$c" -o "$o" &
    CXX_PIDS+=($!)
done
cxx_fail=0
for p in "${CXX_PIDS[@]}"; do wait "$p" || cxx_fail=1; done
(( cxx_fail == 0 )) || { echo "uiview: a source failed to compile" >&2; exit 1; }
g++ -O2 -std=c++17 \
    -o output/uiview/uiview_playback_rml \
    "${CXX_OBJS[@]}" "${C_OBJS[@]}" \
    -Lbuild/rmlui-host-dist/lib \
    -Lbuild/rmlui-host/RmlUi/build \
    -lrmlui -lfreetype -lpng16 -lz -lpthread \
    -Wl,-rpath,/workspace/build/rmlui-host-dist/lib:/workspace/build/rmlui-host/RmlUi/build

export LD_LIBRARY_PATH="/workspace/build/rmlui-host-dist/lib:/workspace/build/rmlui-host/RmlUi/build:${LD_LIBRARY_PATH:-}"

# -----------------------------------------------------------------------------
shopt -s nullglob
rm -f output/uiview/rml_*.bmp   # stale from a prior run

echo "--- rendering all launch + settings screenshots (CPU rasteriser)"
./output/uiview/uiview_playback_rml

# One python start per picture was ~0.3 s each, in a row: convert them side by side.
export KEEP_BMP
printf '%s\n' output/uiview/rml_*.bmp | xargs -P "$(nproc)" -I{} bash -c '
    b="{}"; s="$(basename "${b%.bmp}")"
    python3 tools/shot.py png "$b" "output/uiview/$s.png" >/dev/null
    if [[ "${KEEP_BMP:-0}" != "1" ]]; then rm -f "$b"; fi
    echo "  ok -> output/uiview/$s.png"'

