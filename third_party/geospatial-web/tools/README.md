# third_party/geospatial-web/tools

Local tooling for the geospatial web prototype. No GitHub workflows: everything runs on your machine.

| File | Purpose |
|---|---|
| `geo.py` | Driver: `check`, `bootstrap`, `build`, `test`, `reference`, `serve` |
| `CMakePresets.json` | Presets `native-debug`, `native-release`, `wasm32` |
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
python third_party/geospatial-web/tools/geo.py bootstrap native     # venv, SQLite, PROJ, then build_usd.py
python third_party/geospatial-web/tools/geo.py bootstrap wasm32     # emsdk, SQLite, PROJ, then build_usd.py --build-target wasm
python third_party/geospatial-web/tools/geo.py build native-debug
python third_party/geospatial-web/tools/geo.py test native-debug -- -R usdGeospatial
python third_party/geospatial-web/tools/geo.py test web              # Playwright: W0, W1, W2 specs
```

- `bootstrap <target> --steps a,b` runs a subset (`native`: venv,sqlite,proj,usd-deps; `wasm32`: emsdk,sqlite,proj,usd-deps).
- `build_usd.py` has no deps-only mode, so `usd-deps` also builds an unmodified USD into
  `install/upstream-<target>`. That is the reference "what upstream does" build.
- `bootstrap wasm32 --imaging` passes `--imaging` to `build_usd.py`, which builds OpenSubdiv for WASM. Not needed before C2.
- `build` writes a git-ignored `CMakeUserPresets.json` at the repo root that includes this directory's presets,
  since CMake and CLion only read presets from the source root.
- `test web` runs the app's Playwright specs (installs npm packages and Chromium when missing). `--ui` opens
  Playwright's UI mode to pick, watch and step through tests; `--headed` shows the browser during a normal run;
  anything after `--` goes to Playwright (e.g. `-- -g w2`). The W2 report is `app/reports/latest.html`.
  Rendering uses the GPU; set `GEO_SOFTWARE_GL=1` for software WebGL (see `app/README.md`).
- Every command logs to `cache/logs/<name>-<timestamp>.log` (except interactive `test web --ui` and `serve`).

## Platform notes

- **AlmaLinux 9:** emscripten 5.0.7 needs Python >= 3.10 and the system Python is 3.9. Install
  `python3.12` (AppStream); `geo.py` finds it and sets `EMSDK_PYTHON`. For CLion's `wasm32` profile, export
  `EMSDK_PYTHON=/usr/bin/python3.12` in the environment CLion starts from.
- ccache is optional but worth it: `dnf install ccache` (EPEL). The presets put its cache in `cache/ccache`.
- PROJ for WASM is built static with `-pthread -fexceptions`, the same thread and exception model as USD's WASM
  build, with `proj.db` embedded in `libproj.a` (about 9 MB until C4 trims it).

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
