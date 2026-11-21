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
2. No WASM build of hf, hd, hdar, hdsi, hio, pxOsd, cameraUtil, geomUtil or usdImaging (usdImaging links hdar and hio).
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
     `pxr/imaging/{hd,hdar,hdsi,hf,hio,pxOsd,cameraUtil,geomUtil}`, `pxr/usdImaging/usdImaging`. Drop WebGPU, Hgi, hdSt, Python.
  2. `build_usd.py`: allow `--imaging` and `--usd-imaging` for wasm/wasm64 with GL, Vulkan, Metal, Embree, PTex,
     OpenVDB, OpenImageIO, OCIO, MaterialX off. OpenSubdiv already builds statically for WASM.
  3. CMake: only the GPU-free subset; skip hdSt, hgi*, hdx, usdImagingGL, tools.
  4. Static registration fixes.
  5. Threading caps.
  6. Remaining `__EMSCRIPTEN__` guards (arch, hd, file-system assumptions), one small commit each.
  7. Local check: wasm32 build with `--usd-imaging`, hd/hdsi/usdImaging ctests headless under Node, from `geo.py test wasm32`.

## Autodesk diff, sized (2026-11-21)

Remote `autodesk` = https://github.com/autodesk-forks/USD, branch `adsk/feature/wasm` at 12a01e18d (2026-03-30).
Merge base with our branch: upstream `dev` at ffba29ef1 (2026-03-17), which Autodesk last merged on 2026-03-30, so
`git diff ffba29ef1 autodesk/adsk/feature/wasm` is exactly Autodesk's work: 326 commits, 403 files, +26532 / -1780.
The commit history mixes WebGPU and WASM work, so port hunk by hunk from that diff, not commit by commit.

**Out of scope (the bulk):** `hgiWebGPU` (9.1k lines), `hdEmscripten` render delegate (2.8k), `usdImaging/bin` (2.3k),
`js/` (2.3k), `hdx`, `hdSt`, `hgiInterop`, `hgiGL/Vulkan/Metal`, `garch`, `usdImagingGL`, `usdMtlx`; Dawn, Tint,
glslang, MaterialX, AVIF, zlib/png/jpeg in `build_usd.py`; the generic JS bindings (`wrap*Js.cpp`,
`emscripten*Registration*.h`, `vt/value.*` changes, `PXR_ENABLE_JS_BINDINGS_SUPPORT`); file preloading
(`PXR_ENABLE_PRELOAD_FILES`); wasm64-only and Windows-only fixes; `hd/flattenedPrimvarsDataSource.*` (2023 WebGPU-era
addition upstream does not use); `hd/aov.cpp` and `hio/glslfxResourceLayout.h` (Storm readback/shaders); whitespace.

**Candidates to port (a few hundred lines), one small commit each:**
1. `build_usd.py`: build usdImaging for wasm (Autodesk drops the `not targetWasm` from `buildImaging`/`buildUsdImaging`
   but keeps the explicit `--usd-imaging` error) and OpenSubdiv for wasm (`-DOSD_GPU=OFF`, the emscripten C/CXX flags).
2. `cmake/macros/Public.cmake`, `Private.cmake`: static plugin registration (`IS_STATIC_PLUGIN`, `<lib>_internal`
   targets, whole-archive linking). Compare with what v26.08 already does for emscripten plugins
   (`Public.cmake` "Building ${NAME} plugin as static library for emscripten support") before porting any of it.
3. `pxr/imaging/hio/CMakeLists.txt`: wasm handling (static plugin target, OpenEXR compile definitions; the stbImage
   skip is wasm64-only).
4. `pxr/imaging/CMakeLists.txt`, `pxr/usdImaging/CMakeLists.txt`: our own gating to the GPU-free subset under
   EMSCRIPTEN. Autodesk instead builds everything with WebGPU, so this part is ours, not a port.
5. Only when a build or test hits them: `arch/symbols.cpp` (`ArchGetAddressInfo` returns false on WASM),
   `tf/fileUtils.h` (`ACCESSPERMS`, `DEFFILEMODE`), `tf/atomicRenameUtil.cpp` (umask; only if we write files),
   `tf/stackTrace.h` (`<sys/types.h>`), `cmake/modules/FindOpenSubdiv.cmake` (2 lines). Upstream already builds and
   tests `pxr/base` and `pxr/usd` for wasm32, so most of these may not be needed.
6. `usdImaging/testenv/testUsdImaging.cpp`: a test Autodesk adds; look at it when setting up T10.

## Esri engine limits (for E1)

`projectOperator` / `geographicTransformationUtils` in the Maps SDK do equation-based transforms only:
no grid-based datum transforms, no vertical CRS transforms. Such cases must report "unsupported".
