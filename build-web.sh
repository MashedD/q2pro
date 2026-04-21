#!/usr/bin/env bash
set -Eeuo pipefail
# Prerequisites:
# sudo pacman -S emscripten
# Not sure if needed:
# /usr/lib/emscripten/embuilder build sdl2 libpng zlib
export EMSCRIPTEN=/usr/lib/emscripten
export PATH="$EMSCRIPTEN:$PATH"
export EMCC_FORCE_STDLIBS=1

#rm -rf build-web
meson setup build-web --cross-file cross-web.txt -Dbuildtype=debug
cd build-web
ninja
BASEQ2_DIR=../../../baseq2
GAME_LIB="gamewasm32.so"

if [ ! -f "$GAME_LIB" ]; then
    echo "Missing $GAME_LIB in $(pwd)"
    exit 1
fi

PACKAGER_ARGS=(
    q2pro.data
    --preload "$BASEQ2_DIR"@baseq2
    --preload "$GAME_LIB"@baseq2/"$GAME_LIB"
    --js-output=q2pro_preload.js
    --use-preload-cache
)

python3 "$(em-config EMSCRIPTEN_ROOT)/tools/file_packager.py" "${PACKAGER_ARGS[@]}"

[ -e q2pro.html ] || cat > q2pro.html << 'EOF'
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <title>q2pro Web</title>
    <style>
        body { margin:0; background:#000; overflow:hidden; }
        #canvas { display:block; width:100vw; height:100vh; image-rendering:pixelated; }
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script>
        var Module = {
            canvas: document.getElementById('canvas'),
            print: console.log,
            printErr: console.error,
            onRuntimeInitialized: function() {
                console.log("q2pro runtime initialized");
            }
        };
    </script>

    <script src="q2pro_preload.js"></script>
    <script src="q2pro.js"></script>
</body>
</html>
EOF

emrun q2pro.html
