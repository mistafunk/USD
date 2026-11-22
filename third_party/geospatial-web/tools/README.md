# third_party/geospatial-web/tools

Local tooling for the geospatial web prototype. No GitHub workflows: everything runs on your machine.

| File | Purpose |
|---|---|
| `geo.py` | Driver: `check`, `bootstrap`, `build`, `test`, `reference`, `record`, `usdview`, `serve` |
| `CMakePresets.json` | Presets `native-debug`, `native-release`, `wasm32`, `wasm32-minsize` |
| `cmake/sqlite3/` | CMake build of the SQLite amalgamation (PROJ dependency) |
| `requirements.txt` | Pinned Python packages for the native build's venv |
| `nginx.conf` | COOP/COEP nginx config for serving the built app |

## Workspace

The clone must sit at `<workspace>/src/USD`; the presets find everything else relative to it.
`geo.py` takes the workspace from `USD_GEO_WS`, else the directory two levels above the clone.

```
<workspace>/
  src/USD/                 this clone
  emsdk/                   emsdk 5.0.7
  venv/                    Python for the native build (PySide6, PyOpenGL, Jinja2)
  venv-reference/          usd-core + pyproj for the offline reference (W1)
  deps/native|wasm32/      oneTBB, OpenSubdiv (build_usd.py), SQLite, PROJ (geo.py)
  build/<preset>/          CMake build trees, shared with CLion
  install/<preset>/        our builds
  install/upstream-<target>/  unmodified upstream USD from build_usd.py (reference)
  cache/                   downloads, src, deps-build, ccache, emscripten, logs
```

Pinned versions live at the top of `geo.py`: emsdk 5.0.7, PROJ 9.9.0, SQLite 3.54.1 (with SHA-256).

## First run

```
python third_party/geospatial-web/tools/geo.py check
python third_party/geospatial-web/tools/geo.py bootstrap native     # venv, SQLite, libtiff, PROJ, grids, tinygltf, then build_usd.py
python third_party/geospatial-web/tools/geo.py bootstrap wasm32     # emsdk, the same dependencies, then build_usd.py --build-target wasm
python third_party/geospatial-web/tools/geo.py build native-debug
python third_party/geospatial-web/tools/geo.py test native-debug -- -R usdGeospatial
python third_party/geospatial-web/tools/geo.py test web              # Playwright specs (app/tests)
```

- `bootstrap <target> --steps a,b` runs a subset (`native`: venv,sqlite,tiff,proj,grids,tinygltf,usd-deps;
  `wasm32`: emsdk,sqlite,tiff,proj,grids,tinygltf,usd-deps).
- `build_usd.py` has no deps-only mode, so `usd-deps` also builds an unmodified USD into
  `install/upstream-<target>`. That is the reference "what upstream does" build.
- `bootstrap wasm32 --imaging` passes `--imaging` to `build_usd.py`, which builds OpenSubdiv for WASM. Not needed before C2.
- `build` writes a git-ignored `CMakeUserPresets.json` at the repo root that includes this directory's presets,
  since CMake and CLion only read presets from the source root.
- `test web` runs the app's Playwright specs (installs npm packages and Chromium when missing). `--ui` opens
  Playwright's UI mode to pick, watch and step through tests; `--headed` shows the browser during a normal run;
  anything after `--` goes to Playwright (e.g. `-- -g w2`). The W2 report is `app/reports/latest.html`.
  Rendering uses the GPU; set `GEO_SOFTWARE_GL=1` for software WebGL (see `app/README.md`).
- `build <preset> --reconfigure` reruns CMake's configure step; `--fresh` configures from scratch, which is
  needed when a preset drops a variable (CMake keeps the old cached value otherwise).
- Every command logs to `cache/logs/<name>-<timestamp>.log` (except interactive `test web --ui` and `serve`).

## Size-optimized WebAssembly build

`wasm32-minsize` is `wasm32` compiled and linked with `-Oz` (`MinSizeRel`), in its own build and install trees; it
shares `deps/wasm32`. Build it with `geo.py build wasm32-minsize` (all of USD again: ccache does not help across
flags), test it with `geo.py test wasm32-minsize` and `geo.py test web --wasm-preset wasm32-minsize`, serve it with
`geo.py serve dev --wasm-preset wasm32-minsize` (port 5174, so a dev server of `wasm32` on 5173 is not reused) or
`serve docker --wasm-preset wasm32-minsize`. Both presets install `.gz` and `.br` copies of the module and `proj.db`;
the dev server prefers brotli (Chromium asks for it on localhost), nginx serves gzip.

## Platform notes

- **Python:** the project venv uses Python 3.12 (`VENV_PYTHON` in `geo.py`; AlmaLinux: `dnf install python3.12
  python3.12-devel`), whichever Python runs `geo.py`. A venv with another version is refused; recreate it with
  `geo.py bootstrap native --steps venv --clean`. The presets put the venv first on PATH, so emscripten 5.0.7
  (needs Python >= 3.10; AlmaLinux's system Python is 3.9) works when CLion configures `wasm32` too; `geo.py` also
  sets `EMSDK_PYTHON`.
- ccache is optional but worth it: `dnf install ccache` (EPEL). The presets put its cache in `cache/ccache`.
- PROJ for WASM is built static with `-pthread -fexceptions`, the same thread and exception model as USD's WASM
  build, without embedded resources: the browser fetches `proj.db` (D26), WASM tests mount `deps/wasm32/share/proj`.

## usdview

`geo.py usdview [native-debug|native-release] [--stage FILE] [--crs CRS] [--origin "x, y, z"] [-- <usdview args>]` runs
usdview from a native install with the project venv's Python and the install's `pxr` package, showing `--stage`
(default `testdata/scene/scene.usda`) placed in `--crs` (default `EPSG:4979`) by the geospatial scene index plugin
(`USDGEOSPATIALIMAGING_OUTPUT_CRS`; `--origin` sets `USDGEOSPATIALIMAGING_DISPLAY_ORIGIN`). `--crs none` shows the
unplaced stage. The log prints the environment and command, for an IDE run configuration.

## Browser module

`geo.py build wasm32` installs the OpenUSD browser module (`module/geospatialModule`, an ES6 embind module with
usdImaging, the placement scene index and the PROJ engine) and the test fixtures to
`install/wasm32/share/geospatial-web/{module,fixtures}`. The Vite dev server serves that directory at `/usd`
(workspace from `USD_GEO_WS`, else five levels above `app/`), as nginx serves it in the container.
`geo.py test web -- c4` runs the C4 browser check (`app/c4.html`).

## Grids (D23)

`geo.py bootstrap <target> --steps tiff,proj,grids`: libtiff 4.7.2 (static, zlib only, no system codec packages),
PROJ with TIFF, and the pinned grids from cdn.proj.org: natively all six in `deps/native/share/proj` (PROJ's data
directory, also used by the reference's pyproj), for wasm32 the five browser grids in `deps/wasm32/share/proj-grids`
(not EGM2008, 77 MB). The module install serves those at `/usd/grids` with an `index.json`; the worker fetches the
grids a placement reports missing (`missingGrids`) into `/proj` (PROJ_DATA), resets the engines and places again.
WASM PROJ reads files (no embedded resources): `proj.db` is installed at `/usd/proj` and fetched at startup (D26).
libtiff's CMake config links `ZLIB::ZLIB` without finding zlib: `tools/cmake/zlib-target.cmake` provides it
(`CMAKE_PROJECT_INCLUDE` in the dependency builds and the wasm32 preset, with emscripten's zlib port).

## glTF plugin

`third_party/adobe-usd-gltf` (D24) needs tinygltf's headers: `geo.py bootstrap native|wasm32 --steps tinygltf` (pinned
v2.9.7, part of the full bootstrap). The browser module whole-archives the plugin, since USD only does that for its
own libraries.

## Reference

`geo.py reference -- <command>` runs `reference/reference.py` in `venv-reference`. The first run replaces pyproj's
wheel by a build against the workspace's PROJ (`deps/native`, D19), so reference, native and browser share one PROJ;
`geo.py bootstrap native --steps sqlite,proj` must have run.

## Recorded data from C++

`geo.py record [native-debug]` runs `install/<preset>/bin/geospatialRecord` (from `module/`, option
`PXR_BUILD_GEOSPATIAL_WEB`) on `testdata/scene/scene.usda` and writes `app/public/recorded-cpp/` (git-ignored):
the placement scene index and observer's output in the recorded format. Draw it with `?src=recorded-cpp`, score it
with `GEO_RECORDED_SRC=recorded-cpp geo.py test web -- w1 w2`, compare it with `geo.py reference -- compare`.

## Serving

`geo.py serve docker` runs nginx in Podman (or Docker) on the installed app, with `nginx.conf`:

```
podman run --rm -p 8080:80 \
  -v <workspace>/install/wasm32/share/geospatial-web:/usr/share/nginx/html:ro,z \
  -v <workspace>/src/USD/third_party/geospatial-web/tools/nginx.conf:/etc/nginx/conf.d/default.conf:ro,z \
  docker.io/library/nginx:stable
```

Then open http://localhost:8080 and check `crossOriginIsolated === true` in the console.
`geo.py serve dev` runs the Vite dev server once the web app exists (W0).
