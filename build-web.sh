#!/usr/bin/env bash
set -Eeuo pipefail
# Prerequisites:
# sudo pacman -S emscripten
# /usr/lib/emscripten/embuilder build sdl2 libpng zlib
export EMSCRIPTEN=/usr/lib/emscripten
export PATH="$EMSCRIPTEN:$PATH"

#rm -rf build-web
meson setup build-web --cross-file cross-web.txt -Dbuildtype=release
cd build-web
ninja

python3 $(em-config EMSCRIPTEN_ROOT)/tools/file_packager.py q2pro.data \
    --preload ../../../baseq2@baseq2 \
    --js-output=q2pro_preload.js \
    --use-preload-cache

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
                console.log("✅ q2pro runtime initialized");
            }
        };
    </script>

    <script src="q2pro_preload.js"></script>
    <script src="q2pro.js"></script>
</body>
</html>
EOF

emrun q2pro.html

