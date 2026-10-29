# foverlay
> fullscreen, transparent, click-through browser overlay for linux.

# USE OF AI
This app was created entirely my Codex. I was too lazy to do it myself.

## dev
needs cmake, a c++17 compiler, qt6-base and qt6-webengine. uses x11 or xwayland.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/foverlay 'https://your-overlay-url'
```

ctrl+c to stop.

## options
```bash
./build/foverlay --list-screens
./build/foverlay --screen 1 'https://your-overlay-url'
./build/foverlay --keep-background 'https://your-url'
```

page backgrounds are removed by default. elements with their own backgrounds stay visible.
