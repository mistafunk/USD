# WASM gap on upstream v26.08

Checked 2026-11-21 against upstream `release` (ee47c679, v26.08).

## What upstream already does

- `build_scripts/build_usd.py --build-target wasm|wasm64` builds `pxr/usd` with emscripten (supported since 25.11).
  Upstream CI uses emsdk **5.0.7**.
- Static libraries only, pthreads on (`-pthread`), no Python. `plugInfo.json` files are embedded into the
  virtual file system under `/usd`.
- Example `extras/usd/examples/wasmFetchResolver`: embind, `-sFETCH`, `-sASYNCIFY`; a good template for our
  final link in `third_party/geospatial-web/module`.
- See `BUILDING.md` (WebAssembly section) and `VERSIONS.md`.

## What is missing for option A

1. `build_usd.py` **refuses `--usd-imaging`** for wasm targets. `--imaging` is accepted but never built or tested in CI.
2. No WASM build of hf, hd, hdsi, pxOsd, cameraUtil, geomUtil or usdImaging.
3. Static registration: `TF_REGISTRY_FUNCTION`, scene index plugins and adapter registrations can be dropped by the
   linker. Needs whole-archive linking or explicit anchors plus a startup self-check.
4. Threading: WorkDispatcher/TBB thread counts must be capped to the emscripten pthread pool;
   keep `PROXY_TO_PTHREAD` off so the worker owns the stage.
5. No JS bindings and nothing that consumes a scene index in the browser (our observer + embind).
6. PROJ and SQLite are not part of the build at all (our `usdGeospatialProj` plugin; build from source for wasm).

## Autodesk reference

- Repo: https://github.com/autodesk-forks/USD
- `adsk/feature/wasm`: based on 26.05 dev, last synced 2026-03. Has working emscripten patches for Hydra and usdImaging.
- `adsk/hgiWebGPU`: 26.08 dev (2026-06-15) + 3 commits; WebGPU Hgi backend. **Not used.**
- Use as a reference only, never merge. Steps (PLAN.md, C2):
  1. Diff `adsk/feature/wasm` against upstream v26.05, limited to `build_scripts/`, `cmake/`,
     `pxr/imaging/{hd,hdsi,hf,pxOsd,cameraUtil,geomUtil}`, `pxr/usdImaging/usdImaging`. Drop WebGPU, Hgi, hdSt, Python.
  2. `build_usd.py`: allow `--imaging` and `--usd-imaging` for wasm/wasm64 with GL, Vulkan, Metal, Embree, PTex,
     OpenVDB, OpenImageIO, OCIO, MaterialX off. OpenSubdiv already builds statically for WASM.
  3. CMake: only the GPU-free subset; skip hdSt, hgi*, hdx, usdImagingGL, tools.
  4. Static registration fixes.
  5. Threading caps.
  6. Remaining `__EMSCRIPTEN__` guards (arch, hd, file-system assumptions), one small commit each.
  7. Local check: wasm32 build with `--usd-imaging`, hd/hdsi/usdImaging ctests headless under Node, from `geo.py test wasm32`.

## Esri engine limits (for E1)

`projectOperator` / `geographicTransformationUtils` in the Maps SDK do equation-based transforms only:
no grid-based datum transforms, no vertical CRS transforms. Such cases must report "unsupported".
