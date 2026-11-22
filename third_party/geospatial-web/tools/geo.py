#!/usr/bin/env python3
"""Local driver for the OpenUSD geospatial web prototype.

All builds and tests run on this machine; there is no GitHub CI. The workspace
root comes from USD_GEO_WS (default: the directory two levels above this clone
when it sits at <workspace>/src/USD, else ~/usd-geo):

    <workspace>/
      src/USD/          this clone
      emsdk/            emsdk, pinned below
      venv/             Python for the native build (usdview, usdGenSchema)
      venv-reference/   usd-core + pyproj for the offline reference
      deps/<target>/    third-party deps (oneTBB, OpenSubdiv, SQLite, PROJ)
      build/<preset>/   CMake build trees (shared with CLion)
      install/<preset>/ installs; install/upstream-<target> is build_usd.py's
      cache/            downloads, dep builds, ccache, emscripten cache, logs

Usage:
    python third_party/geospatial-web/tools/geo.py check
    python third_party/geospatial-web/tools/geo.py bootstrap native|wasm32 [--steps ...]
    python third_party/geospatial-web/tools/geo.py build native-debug|native-release|wasm32
    python third_party/geospatial-web/tools/geo.py test <preset> [-- <ctest args>]
    python third_party/geospatial-web/tools/geo.py test web [--ui | --headed] [-- <playwright args>]
    python third_party/geospatial-web/tools/geo.py reference [-- <args>]
    python third_party/geospatial-web/tools/geo.py record [native-debug|native-release] [-- <args>]
    python third_party/geospatial-web/tools/geo.py usdview [native-debug|native-release] [--stage FILE] [--crs CRS] [--origin "x, y, z"] [-- <usdview args>]
    python third_party/geospatial-web/tools/geo.py serve dev|docker
"""

import argparse
import datetime
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

# Pinned versions. Native and WASM use the same PROJ and SQLite sources.
EMSDK_VERSION = "5.0.7"
EMSDK_REPO = "https://github.com/emscripten-core/emsdk.git"
PROJ = dict(
    name="proj-9.9.0",
    url="https://download.osgeo.org/proj/proj-9.9.0.tar.gz",
    sha256="791a0610547eeabb17006cfd49cdbd2034f3240f47ed5e88a1031811f4e2bcf3")
SQLITE = dict(
    name="sqlite-amalgamation-3540100",
    url="https://sqlite.org/2026/sqlite-amalgamation-3540100.zip",
    sha256="0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9")
# GeoTIFF grid support in PROJ (D23).
TIFF = dict(
    name="tiff-4.7.2",
    url="https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz",
    sha256="672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a")
# PROJ grids the tests and the first real data need (D23), from PROJ's CDN.
# "browser": also served to the web app, which fetches them on demand.
GRIDS = [
    # EGM96 heights: ArcGIS 3D object layers with vcsWkid 5773 (Zurich).
    dict(name="us_nga_egm96_15.tif", browser=True,
         sha256="db493027562c9b004d7220fa881f5603adada4e1c5029b933fa7de4547b0e78d"),
    # NAD27 -> NAD83 (NADCON) and NAD83 -> WGS 84 in California (HPGN): T3.
    dict(name="us_noaa_conus.tif", browser=True,
         sha256="44611d823c48e5347500ee6afe40ff33d2b88cf817bf59f705ed4a4c3bd687d7"),
    dict(name="us_noaa_cshpgn.tif", browser=True,
         sha256="d54183c832e0366fc558147961bd8ae7d0dd3b5ae945069afd6a37a5a5c604c9"),
    # NAVD88 heights (GEOID99) and NAD83 -> NAD83(HARN) (NADCON5): T4.
    dict(name="us_noaa_g1999u05.tif", browser=True,
         sha256="3b9325111c21b42e4e254ff1455db825587abc530188a94f9628321969797139"),
    dict(name="us_noaa_nadcon5_nad83_1986_nad83_harn_conus.tif", browser=True,
         sha256="e12ac2e886edafe616e0994c610ad3daa01db17c00d0e9938145d956f249fc2f"),
    # EGM2008, the proposal's default height for 2D CRSs: native only (77 MB).
    dict(name="us_nga_egm08_25.tif", browser=False,
         sha256="4191d471eefebf24091b56dbc604353cb3b8cf8cc70e448bb9ae56a272bef17a"),
]
GRID_URL = "https://cdn.proj.org/"
# Header-only, for the Adobe glTF file format plugin (third_party/adobe-usd-gltf).
TINYGLTF = dict(
    name="tinygltf-2.9.7",
    url="https://github.com/syoyo/tinygltf/archive/refs/tags/v2.9.7.tar.gz",
    sha256="bf92fe53b86302f9415e77504ecd81a58659292ed04dffec873f5c71665046a0")
CMAKE_MIN = (3, 27)
# Python of the project venv: the native build's Python bindings, usdview,
# usdGenSchema, and (via PATH) emcc, which needs >= 3.10.
VENV_PYTHON = (3, 12)

TARGETS = ("native", "wasm32")
PRESETS = {"native-debug": "native", "native-release": "native",
           "wasm32": "wasm32"}

SCRIPT_DIR = Path(__file__).resolve().parent
GEO_DIR = SCRIPT_DIR.parent
REPO = next(p for p in SCRIPT_DIR.parents if (p / "pxr").is_dir())
WEB_APP = GEO_DIR / "app"
# Where the wasm32 install puts the servable app (set by module/ in C4).
SITE_SUBDIR = Path("share") / "geospatial-web"


def _default_workspace():
    if REPO.parent.name == "src":
        return REPO.parent.parent
    return Path.home() / "usd-geo"


WS = Path(os.environ.get("USD_GEO_WS") or _default_workspace())
WS = WS.expanduser().resolve()
CACHE = WS / "cache"
LOGS = CACHE / "logs"
DOWNLOADS = CACHE / "downloads"
EMSDK = WS / "emsdk"
VENV = WS / "venv"
VENV_REFERENCE = WS / "venv-reference"


def deps_dir(target):
    return WS / "deps" / target


def dep_build_dir(target):
    return CACHE / "deps-build" / target


def is_windows():
    return platform.system() == "Windows"


def venv_python(venv):
    if is_windows():
        return venv / "Scripts" / "python.exe"
    return venv / "bin" / "python3"


def exe(name):
    return name + ".exe" if is_windows() else name


# --------------------------------------------------------------------------
# Output and processes

class Log:
    """Tees subprocess output to the console and cache/logs/<name>-<ts>.log."""

    def __init__(self, name):
        LOGS.mkdir(parents=True, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.path = LOGS / "{}-{}.log".format(name, stamp)
        self.file = self.path.open("w", encoding="utf-8")
        self.start = time.monotonic()
        info("log: {}".format(self.path))

    def write(self, text):
        sys.stdout.write(text)
        sys.stdout.flush()
        self.file.write(text)
        self.file.flush()

    def close(self, ok):
        elapsed = int(time.monotonic() - self.start)
        self.write("\n[geo] {} after {}m{:02d}s, log: {}\n".format(
            "done" if ok else "FAILED", elapsed // 60, elapsed % 60,
            self.path))
        self.file.close()


def info(msg):
    print("[geo] " + msg, flush=True)


def fail(msg):
    print("[geo] error: " + msg, file=sys.stderr, flush=True)
    sys.exit(1)


def run(cmd, log, cwd=None, env=None):
    cmd = [str(c) for c in cmd]
    log.write("\n[geo] $ {}\n".format(" ".join(cmd)))
    proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    for line in proc.stdout:
        log.write(line)
    if proc.wait() != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)


def logged(name, fn, *args):
    log = Log(name)
    try:
        fn(log, *args)
    except subprocess.CalledProcessError as e:
        log.close(False)
        fail("command exited with {}: {}".format(e.returncode,
                                                 " ".join(e.cmd)))
    except BaseException:
        log.close(False)
        raise
    log.close(True)


# --------------------------------------------------------------------------
# Environments

def base_env():
    env = dict(os.environ)
    env["USD_GEO_WS"] = str(WS)
    env["CCACHE_DIR"] = str(CACHE / "ccache")
    return env


def emsdk_env():
    emscripten = EMSDK / "upstream" / "emscripten"
    if not emscripten_toolchain().exists():
        fail("emsdk not installed at {}; run: geo.py bootstrap wasm32"
             .format(EMSDK))
    env = base_env()
    paths = [emscripten, EMSDK]
    node_bins = sorted((EMSDK / "node").glob("*/bin"))
    paths += node_bins
    env["PATH"] = os.pathsep.join([str(p) for p in paths] + [env["PATH"]])
    env["EMSDK"] = str(EMSDK)
    if node_bins:
        # USD's test registration runs WASM tests with this node.
        env["EMSDK_NODE"] = str(node_bins[-1] / exe("node"))
    env["EM_CACHE"] = str(CACHE / "emscripten")
    if not env.get("EMSDK_PYTHON"):
        py = find_python((3, 10))
        if not py:
            fail("emscripten needs Python >= 3.10; install one (AlmaLinux: "
                 "dnf install python3.12) or set EMSDK_PYTHON")
        env["EMSDK_PYTHON"] = py
    return env


def find_python(minimum):
    """A Python >= minimum: this one, else python3.N on PATH (newest first).
    emcc needs >= 3.10 (honours EMSDK_PYTHON; emsdk bundles a Python on
    Windows and macOS but not on Linux); the reference needs >= 3.12."""
    if sys.version_info >= minimum:
        return sys.executable
    for minor in range(14, minimum[1] - 1, -1):
        found = shutil.which("python3.{}".format(minor))
        if found:
            return found
    return None


def native_env():
    """Puts the venv first on PATH; build_usd.py looks for pyside6-uic."""
    env = base_env()
    env["VIRTUAL_ENV"] = str(VENV)
    env["PATH"] = os.pathsep.join(
        [str(venv_python(VENV).parent), env["PATH"]])
    return env


def target_env(target):
    return emsdk_env() if target == "wasm32" else native_env()


def emscripten_toolchain():
    return (EMSDK / "upstream" / "emscripten" / "cmake" / "Modules" /
            "Platform" / "Emscripten.cmake")


# --------------------------------------------------------------------------
# Downloads

def download(pkg):
    """Downloads a pinned file once and checks it; returns its path."""
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    archive = DOWNLOADS / pkg["url"].rsplit("/", 1)[1]
    if not archive.exists():
        info("downloading {}".format(pkg["url"]))
        tmp = archive.with_name(archive.name + ".part")
        with urllib.request.urlopen(pkg["url"]) as r, tmp.open("wb") as f:
            shutil.copyfileobj(r, f)
        tmp.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != pkg["sha256"]:
        archive.unlink()
        fail("checksum mismatch for {} (got {})".format(archive.name, digest))
    return archive


def fetch(pkg):
    """Downloads and unpacks a pinned archive; returns the source directory."""
    archive = download(pkg)
    src_root = CACHE / "src"
    src = src_root / pkg["name"]
    if not src.exists():
        src_root.mkdir(parents=True, exist_ok=True)
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as z:
                z.extractall(src_root)
        else:
            with tarfile.open(archive) as t:
                if hasattr(tarfile, "data_filter"):
                    t.extractall(src_root, filter="data")
                else:
                    t.extractall(src_root)
    if not src.is_dir():
        fail("{} did not unpack to {}".format(archive.name, src))
    return src


# --------------------------------------------------------------------------
# CMake helpers for the deps geo.py builds itself (SQLite, PROJ)

def cmake_cache_source(build):
    """Source directory a CMake build tree was configured from, or None."""
    cache = build / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:"):
            return Path(line.split("=", 1)[1])
    return None


def cmake_dep(log, target, name, src, args, jobs):
    prefix = deps_dir(target)
    build = dep_build_dir(target) / name
    old_src = cmake_cache_source(build)
    if old_src and old_src.resolve() != Path(src).resolve():
        # The source moved (e.g. a directory was renamed); CMake refuses
        # to reuse the tree, so start it fresh.
        log.write("[geo] {} was configured from {}; removing it\n"
                  .format(build, old_src))
        shutil.rmtree(build)
    cmd = ["cmake", "-S", src, "-B", build, "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_INSTALL_PREFIX={}".format(prefix),
           "-DCMAKE_PREFIX_PATH={}".format(prefix),
           "-DCMAKE_INSTALL_LIBDIR=lib",
           # ZLIB::ZLIB for libtiff's CMake config (see the file).
           "-DCMAKE_PROJECT_INCLUDE={}".format(SCRIPT_DIR / "cmake" / "zlib-target.cmake")]
    if target == "wasm32":
        cmd += ["-DCMAKE_TOOLCHAIN_FILE={}".format(emscripten_toolchain()),
                "-DCMAKE_FIND_ROOT_PATH={}".format(prefix),
                # Same threading and exception model as USD's WASM build
                # (cmake/defaults/ProjectDefaults.cmake); PROJ throws and
                # catches internally.
                "-DCMAKE_C_FLAGS=-pthread -fexceptions",
                "-DCMAKE_CXX_FLAGS=-pthread -fexceptions",
                "-DCMAKE_EXE_LINKER_FLAGS=-pthread -fexceptions"]
    env = target_env(target)
    run(cmd + args, log, env=env)
    run(["cmake", "--build", build, "--target", "install", "-j", jobs],
        log, env=env)


def host_sqlite3():
    """sqlite3 shell that runs on the build machine; PROJ needs it."""
    ours = deps_dir("native") / "bin" / exe("sqlite3")
    if ours.exists():
        return ours
    found = shutil.which("sqlite3")
    if found:
        return Path(found)
    fail("no host sqlite3 found; run: geo.py bootstrap native --steps sqlite")


# --------------------------------------------------------------------------
# bootstrap

def python_exact(version):
    """This interpreter if it is Python major.minor, else python3.N on PATH."""
    if sys.version_info[:2] == version:
        return sys.executable
    return shutil.which("python{}.{}".format(*version))


def venv_version(venv):
    py = venv_python(venv)
    if not py.exists():
        return None
    out = subprocess.run([str(py), "-c", "import sys; print(*sys.version_info[:2])"],
                         capture_output=True, text=True)
    return tuple(int(v) for v in out.stdout.split()) if out.returncode == 0 else None


def step_venv(log, target, args):
    want = "{}.{}".format(*VENV_PYTHON)
    have = venv_version(VENV)
    if have and have != VENV_PYTHON:
        fail("{} uses Python {}.{}, the project needs {}; recreate it with: "
             "geo.py bootstrap native --steps venv --clean".format(
                 VENV, *have, want))
    if not have:
        base = python_exact(VENV_PYTHON)
        if not base:
            fail("Python {} not found (AlmaLinux: dnf install python{} "
                 "python{}-devel)".format(want, want, want))
        run([base, "-m", "venv", VENV], log)
    py = venv_python(VENV)
    run([py, "-m", "pip", "install", "--upgrade", "pip"], log)
    run([py, "-m", "pip", "install", "-r", SCRIPT_DIR / "requirements.txt"],
        log)


def step_emsdk(log, target, args):
    if not (EMSDK / "emsdk.py").exists():
        run(["git", "clone", EMSDK_REPO, EMSDK], log)
    run(["git", "-C", EMSDK, "fetch", "--tags"], log)
    run(["git", "-C", EMSDK, "checkout", "--detach", EMSDK_VERSION], log)
    for action in ("install", "activate"):
        run([sys.executable, EMSDK / "emsdk.py", action, EMSDK_VERSION], log,
            cwd=EMSDK)


def step_sqlite(log, target, args):
    src = fetch(SQLITE)
    cmake_dep(log, target, "sqlite3", SCRIPT_DIR / "cmake" / "sqlite3",
              ["-DSQLITE_SOURCE_DIR={}".format(src)], args.jobs)


def step_tinygltf(log, target, args):
    """Installs tinygltf's headers to deps/<target>/include/tinygltf."""
    src = fetch(TINYGLTF)
    dst = deps_dir(target) / "include" / "tinygltf"
    dst.mkdir(parents=True, exist_ok=True)
    for name in ("tiny_gltf.h", "json.hpp", "stb_image.h", "stb_image_write.h"):
        shutil.copy2(src / name, dst / name)
    log.write("[geo] tinygltf headers in {}\n".format(dst))


def emscripten_zlib(log):
    """zlib from emscripten's ports (built into the emscripten cache)."""
    env = emsdk_env()
    run([exe("embuilder") if is_windows() else "embuilder", "build", "zlib"], log, env=env)
    sysroot = Path(env["EM_CACHE"]) / "sysroot"
    return ["-DZLIB_INCLUDE_DIR={}".format(sysroot / "include"),
            "-DZLIB_LIBRARY={}".format(sysroot / "lib" / "wasm32-emscripten" / "libz.a")]


def step_tiff(log, target, args):
    """Static libtiff with zlib only (PROJ's GeoTIFF grids), for PROJ."""
    src = fetch(TIFF)
    opts = ["-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-Dtiff-tools=OFF", "-Dtiff-tests=OFF", "-Dtiff-contrib=OFF",
            "-Dtiff-docs=OFF", "-Dcxx=OFF", "-Dzlib=ON", "-Dlibdeflate=OFF",
            "-Djpeg=OFF", "-Dold-jpeg=OFF", "-Djbig=OFF", "-Dlerc=OFF",
            "-Dlzma=OFF", "-Dzstd=OFF", "-Dwebp=OFF", "-Dpixarlog=OFF"]
    # Do not look for the codecs at all: system packages are not part of the
    # pinned build (and AlmaLinux's libjpeg-turbo CMake package can be broken).
    opts += ["-DCMAKE_DISABLE_FIND_PACKAGE_{}=ON".format(pkg) for pkg in
             ("JPEG", "libjpeg-turbo", "Deflate", "LibLZMA", "ZSTD", "WebP",
              "JBIG", "LERC",
              "OpenGL", "GLUT")]
    if target == "wasm32":
        opts += emscripten_zlib(log)
    cmake_dep(log, target, "tiff", src, opts, args.jobs)


def step_grids(log, target, args):
    """Pinned PROJ grids: natively into PROJ's data directory (found without
    configuration, also by the reference's pyproj); for wasm32 the browser
    set into share/proj-grids, which the module install serves."""
    if target == "native":
        dst = deps_dir(target) / "share" / "proj"
        grids = GRIDS
    else:
        dst = deps_dir(target) / "share" / "proj-grids"
        grids = [g for g in GRIDS if g["browser"]]
    dst.mkdir(parents=True, exist_ok=True)
    for g in grids:
        path = download(dict(g, url=GRID_URL + g["name"]))
        shutil.copy2(path, dst / g["name"])
        log.write("[geo] {} -> {}\n".format(g["name"], dst))


def step_proj(log, target, args):
    src = fetch(PROJ)
    prefix = deps_dir(target)
    lib = prefix / "lib" / ("sqlite3.lib" if is_windows() and
                            target == "native" else "libsqlite3.a")
    common = [
        "-DSQLite3_INCLUDE_DIR={}".format(prefix / "include"),
        "-DSQLite3_LIBRARY={}".format(lib),
        "-DEXE_SQLITE3={}".format(host_sqlite3()),
        "-DENABLE_TIFF=ON",
        "-DENABLE_CURL=OFF",
        "-DBUILD_TESTING=OFF",
    ]
    if target == "native":
        # Shared, so the usdGeospatialProj plugin can link it; apps
        # (projinfo, cs2cs) are handy for checking results by hand.
        extra = ["-DBUILD_SHARED_LIBS=ON", "-DBUILD_APPS=ON",
                 "-DBUILD_PROJSYNC=OFF"]
    else:
        # Static, without embedded resources: proj.db (10.7 MB) and the grids
        # are files in PROJ_DATA. The browser worker fetches proj.db at
        # startup and grids on demand (D23, D26); WASM tests mount the
        # installed share/proj under Node.
        extra = ["-DBUILD_SHARED_LIBS=OFF", "-DBUILD_APPS=OFF",
                 "-DEMBED_RESOURCE_FILES=OFF"] + emscripten_zlib(log)
    cmake_dep(log, target, "proj", src, common + extra, args.jobs)


def step_usd_deps(log, target, args):
    """build_usd.py builds oneTBB and OpenSubdiv into deps/<target>.

    build_usd.py has no deps-only mode, so it also builds and installs an
    upstream USD into install/upstream-<target>, kept as a reference build.
    """
    build_root = dep_build_dir(target) / "build_usd"
    cmd = [REPO / "build_scripts" / "build_usd.py",
           WS / "install" / "upstream-{}".format(target),
           "--inst", deps_dir(target),
           "--src", build_root / "src",
           "--build", build_root / "build",
           "--generator", "Ninja", "--onetbb", "--no-materialx",
           "--no-tests", "--no-examples", "--no-tutorials", "--no-docs",
           "-j", args.jobs]
    if target == "native":
        py = venv_python(VENV)
        if not py.exists():
            fail("venv missing; run: geo.py bootstrap native --steps venv")
        cmd = [py] + cmd + ["--python", "--usd-imaging", "--usdview"]
    else:
        cmd = [sys.executable] + cmd + ["--build-target", "wasm"]
        if args.imaging:
            # OpenSubdiv for WASM, and the GPU-free imaging libraries and
            # usdImaging in the upstream reference build (C2).
            cmd += ["--usd-imaging"]
    # OpenSubdiv's CMake otherwise picks the newest python3.x on PATH; use
    # the interpreter that runs build_usd.py.
    cmd += ["--build-args",
            "OpenSubdiv,-DPython_EXECUTABLE={}".format(cmd[0])]
    run(cmd, log, env=target_env(target))


# Build trees under cache/deps-build/<target> that each step owns.
STEP_BUILD_DIRS = {"sqlite": "sqlite3", "tiff": "tiff", "proj": "proj",
                   "usd-deps": "build_usd"}

BOOTSTRAP_STEPS = {
    "native": [("venv", step_venv), ("sqlite", step_sqlite), ("tiff", step_tiff),
               ("proj", step_proj), ("grids", step_grids),
               ("tinygltf", step_tinygltf), ("usd-deps", step_usd_deps)],
    "wasm32": [("emsdk", step_emsdk), ("sqlite", step_sqlite), ("tiff", step_tiff),
               ("proj", step_proj), ("grids", step_grids),
               ("tinygltf", step_tinygltf), ("usd-deps", step_usd_deps)],
}


def cmd_bootstrap(args):
    steps = BOOTSTRAP_STEPS[args.target]
    names = [n for n, _ in steps]
    wanted = args.steps.split(",") if args.steps else names
    unknown = set(wanted) - set(names)
    if unknown:
        fail("unknown steps {}; {} has: {}".format(
            sorted(unknown), args.target, ", ".join(names)))
    info("workspace {}".format(WS))

    def body(log):
        if args.clean and "venv" in wanted and VENV.exists():
            log.write("[geo] --clean: removing {}\n".format(VENV))
            shutil.rmtree(VENV)
        if args.clean:
            for name in wanted:
                build = dep_build_dir(args.target) / STEP_BUILD_DIRS.get(
                    name, "")
                if name in STEP_BUILD_DIRS and build.exists():
                    log.write("[geo] --clean: removing {}\n".format(build))
                    shutil.rmtree(build)
        for name, fn in steps:
            if name in wanted:
                log.write("\n[geo] === bootstrap {}: {}\n".format(
                    args.target, name))
                fn(log, args.target, args)

    logged("bootstrap-{}".format(args.target), body)


# --------------------------------------------------------------------------
# build / test

def ensure_user_presets():
    """CMake only reads presets from the source root, so a local
    CMakeUserPresets.json includes ours. It is listed in .git/info/exclude."""
    user = REPO / "CMakeUserPresets.json"
    rel = (SCRIPT_DIR / "CMakePresets.json").relative_to(REPO).as_posix()
    if user.exists():
        data = json.loads(user.read_text(encoding="utf-8"))
        if rel in data.get("include", []):
            return
        fail("{} exists without including {}; add it to 'include'"
             .format(user, rel))
    user.write_text(json.dumps({"version": 6, "include": [rel]}, indent=2)
                    + "\n", encoding="utf-8")
    exclude = REPO / ".git" / "info" / "exclude"
    if exclude.parent.is_dir():
        lines = exclude.read_text(encoding="utf-8").splitlines() \
            if exclude.exists() else []
        if "/CMakeUserPresets.json" not in lines:
            with exclude.open("a", encoding="utf-8") as f:
                f.write("/CMakeUserPresets.json\n")
    info("wrote {}".format(user))


def check_layout():
    if REPO != WS / "src" / "USD":
        fail("the CMake presets expect this clone at {}, but it is at {}"
             .format(WS / "src" / "USD", REPO))


def cmd_build(args):
    check_layout()
    ensure_user_presets()
    target = PRESETS[args.preset]
    if not deps_dir(target).is_dir():
        fail("no deps for {}; run: geo.py bootstrap {}".format(target, target))
    env = target_env(target)
    build = WS / "build" / args.preset

    def body(log):
        if (args.reconfigure or args.fresh or
                not (build / "CMakeCache.txt").exists()):
            # --fresh drops the cache, e.g. after a preset removed a variable
            # (a plain reconfigure keeps the old cached value).
            run(["cmake", "--preset", args.preset] +
                (["--fresh"] if args.fresh else []), log, cwd=REPO, env=env)
        cmd = ["cmake", "--build", "--preset", args.preset, "-j", args.jobs]
        if args.target:
            cmd += ["--target"] + args.target
        run(cmd, log, cwd=REPO, env=env)

    logged(args.preset, body)


def split_test_args(args):
    """argparse.REMAINDER also swallows our own flags after the preset name;
    take --ui and --headed back out of the part before '--'."""
    rest = list(args.ctest_args)
    cut = rest.index("--") if "--" in rest else len(rest)
    own, passthrough = rest[:cut], rest[cut + 1:]
    keep = []
    for a in own:
        if a in ("--ui", "--headed"):
            setattr(args, a[2:], True)
        else:
            keep.append(a)
    args.ctest_args = keep + passthrough


def cmd_test(args):
    split_test_args(args)
    if args.preset == "web":
        return cmd_test_web(args)
    if args.ui or args.headed:
        fail("--ui and --headed only apply to: geo.py test web")
    check_layout()
    ensure_user_presets()
    env = target_env(PRESETS[args.preset])
    if not (WS / "build" / args.preset / "CTestTestfile.cmake").exists():
        fail("nothing built for {}; run: geo.py build {}".format(
            args.preset, args.preset))
    extra = args.ctest_args
    logged("test-" + args.preset, lambda log: run(
        ["ctest", "--preset", args.preset, "-j", args.jobs] + extra,
        log, cwd=REPO, env=env))


def web_tool(name):
    """npm or npx for the web app, with its node_modules installed."""
    if not (WEB_APP / "package.json").exists():
        fail("web app not set up yet (milestone W0): {}".format(WEB_APP))
    tool = shutil.which(name)
    if not tool:
        fail("{} not found; install Node.js LTS".format(name))
    if not (WEB_APP / "node_modules").exists():
        subprocess.run([shutil.which("npm") or "npm", "ci"], cwd=WEB_APP,
                       env=base_env(), check=True)
    return tool


def cmd_test_web(args):
    """Playwright specs in the web app (W0, W1, W2). Headless runs are
    logged; --ui opens Playwright's UI mode (pick, watch and step through
    tests), --headed shows the browser during a normal run."""
    npx = web_tool("npx")
    env = base_env()
    extra = args.ctest_args
    # Downloads the pinned Chromium once; a no-op when it is present.
    subprocess.run([npx, "playwright", "install", "chromium"], cwd=WEB_APP,
                   env=env, check=True)
    cmd = [npx, "playwright", "test"] + extra
    if args.ui:
        sys.exit(subprocess.call(cmd + ["--ui"], cwd=WEB_APP, env=env))
    if args.headed:
        cmd.append("--headed")
    logged("test-web", lambda log: run(cmd, log, cwd=WEB_APP, env=env))
    info("harness report: {}".format(WEB_APP / "reports" / "latest.html"))


# --------------------------------------------------------------------------
# reference / serve

def reference_env():
    """pyproj built against and running on the workspace's native PROJ, so
    the reference uses the same PROJ as our builds (D19)."""
    deps = WS / "deps" / "native"
    env = base_env()
    env["PROJ_DIR"] = str(deps)
    env["PROJ_DATA"] = str(deps / "share" / "proj")
    if is_windows():
        var, lib = "PATH", deps / "bin"
    elif platform.system() == "Darwin":
        var, lib = "DYLD_LIBRARY_PATH", deps / "lib"
    else:
        var, lib = "LD_LIBRARY_PATH", deps / "lib"
    env[var] = os.pathsep.join([str(lib)] + ([env[var]] if env.get(var) else []))
    return env


def pyproj_proj_version(py, env):
    r = subprocess.run([str(py), "-c", "import pyproj; print(pyproj.proj_version_str)"],
                       env=env, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def cmd_reference(args):
    ref = GEO_DIR / "reference"
    reqs = ref / "requirements.txt"
    script = ref / "reference.py"
    if not reqs.exists() or not script.exists():
        fail("offline reference not set up yet (milestone W1): {}"
             .format(ref))
    extra = [a for a in args.ref_args if a != "--"]

    def body(log):
        # Same Python as the project venv (usd-core and pyproj pins need
        # >= 3.12). Cheap to rebuild, so a version mismatch recreates it.
        py = venv_python(VENV_REFERENCE)
        have = venv_version(VENV_REFERENCE)
        if have and have != VENV_PYTHON:
            log.write("[geo] {} uses Python {}.{}; recreating it with {}.{}\n"
                      .format(VENV_REFERENCE, *have, *VENV_PYTHON))
            shutil.rmtree(VENV_REFERENCE)
            have = None
        if not have:
            base = python_exact(VENV_PYTHON)
            if not base:
                fail("Python {}.{} not found (AlmaLinux: dnf install "
                     "python{}.{})".format(*VENV_PYTHON, *VENV_PYTHON))
            run([base, "-m", "venv", VENV_REFERENCE], log)
        env = reference_env()
        if not (WS / "deps" / "native" / "share" / "proj" / "proj.db").exists():
            fail("native PROJ not built; run: geo.py bootstrap native "
                 "--steps sqlite,proj")
        run([py, "-m", "pip", "install", "-q", "-r", reqs], log, env=env)
        # requirements.txt pins pyproj; replace its wheel (bundled PROJ) by a
        # build against ours once.
        want = PROJ["name"].split("-")[1]
        have_proj = pyproj_proj_version(py, env)
        if have_proj != want:
            log.write("[geo] pyproj uses PROJ {}; building it against PROJ {}\n"
                      .format(have_proj, want))
            pin = next(line.strip() for line in reqs.read_text().splitlines()
                       if line.startswith("pyproj=="))
            run([py, "-m", "pip", "install", "-q", "--no-binary", "pyproj",
                 "--force-reinstall", "--no-deps", "--no-cache-dir", pin],
                log, env=env)
            have_proj = pyproj_proj_version(py, env)
            if have_proj != want:
                fail("pyproj still uses PROJ {} (wanted {})".format(have_proj, want))
        run([py, script] + extra, log, cwd=ref, env=env)

    logged("reference", body)


def cmd_record(args):
    """The C++ placement of the test scene, through the scene index observer,
    in the recorded format: app/public/recorded-cpp (?src=recorded-cpp)."""
    tool = WS / "install" / args.preset / "bin" / exe("geospatialRecord")
    if not tool.exists():
        fail("{} not built; run: geo.py build {}".format(tool, args.preset))
    extra = [a for a in args.record_args if a != "--"]
    if not extra:
        extra = [GEO_DIR / "testdata" / "scene" / "scene.usda",
                 WEB_APP / "public" / "recorded-cpp"]
    env = target_env(PRESETS[args.preset])
    logged("record-" + args.preset,
           lambda log: run([tool] + extra, log, cwd=REPO, env=env))


def cmd_usdview(args):
    """usdview from a native install with the project venv, showing the
    stage placed in an output CRS by the geospatial scene index plugin."""
    install = WS / "install" / args.preset
    usdview = install / "bin" / "usdview"
    if not usdview.exists():
        fail("{} not built; run: geo.py build {}".format(usdview, args.preset))
    # The pxr package's directory differs per platform (lib/python3.12/
    # site-packages, lib/python); find it.
    packages = sorted(install.glob("lib/**/pxr/__init__.py"))
    if not packages:
        fail("no pxr Python package in {}".format(install / "lib"))
    env = native_env()
    env["PYTHONPATH"] = os.pathsep.join(
        [str(packages[0].parent.parent)] +
        ([env["PYTHONPATH"]] if env.get("PYTHONPATH") else []))
    if is_windows():
        # USD's DLLs.
        env["PATH"] = os.pathsep.join(
            [str(install / "bin"), str(install / "lib"), env["PATH"]])
    for name in ("USDGEOSPATIALIMAGING_OUTPUT_CRS",
                 "USDGEOSPATIALIMAGING_DISPLAY_ORIGIN"):
        env.pop(name, None)
    if args.crs.lower() != "none":
        env["USDGEOSPATIALIMAGING_OUTPUT_CRS"] = args.crs
        if args.origin:
            env["USDGEOSPATIALIMAGING_DISPLAY_ORIGIN"] = args.origin
    stage = Path(args.stage) if args.stage else (
        GEO_DIR / "testdata" / "scene" / "scene.usda")
    cmd = [venv_python(VENV), usdview, stage.resolve()] + args.usdview_args

    def body(log):
        settings = " ".join("{}={}".format(k, shlex.quote(env[k])) for k in
                            ("USDGEOSPATIALIMAGING_OUTPUT_CRS",
                             "USDGEOSPATIALIMAGING_DISPLAY_ORIGIN",
                             "PYTHONPATH") if k in env)
        log.write("[geo] environment: {}\n".format(settings))
        run(cmd, log, cwd=REPO, env=env)

    logged("usdview-" + args.preset, body)


def container_tool():
    for tool in ("podman", "docker"):
        if shutil.which(tool):
            return tool
    fail("neither podman nor docker found")


def cmd_serve(args):
    if args.mode == "dev":
        npm = web_tool("npm")
        sys.exit(subprocess.call([npm, "run", "dev"], cwd=WEB_APP, env=base_env()))

    site = WS / "install" / "wasm32" / SITE_SUBDIR
    if not site.is_dir():
        fail("nothing installed at {}; run: geo.py build wasm32".format(site))
    # SELinux (AlmaLinux) needs the bind mounts relabelled.
    opts = "ro,z" if platform.system() == "Linux" else "ro"
    cmd = [container_tool(), "run", "--rm", "-p", "{}:80".format(args.port),
           "-v", "{}:/usr/share/nginx/html:{}".format(site, opts),
           "-v", "{}:/etc/nginx/conf.d/default.conf:{}".format(
               SCRIPT_DIR / "nginx.conf", opts),
           "docker.io/library/nginx:stable"]
    info("serving {} at http://localhost:{}".format(site, args.port))
    sys.exit(subprocess.call(cmd))


# --------------------------------------------------------------------------
# check

def tool_version(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    text = (out.stdout or out.stderr).strip().splitlines()
    return text[0] if text else ""


def cmd_check(args):
    ok = True

    def report(label, value, good=True, hint="", optional=False):
        nonlocal ok
        ok = ok and (good or optional)
        mark = "ok " if good else "!! "
        print("  {}{:<22} {}{}".format(mark, label, value or "not found",
                                      "  ({})".format(hint) if hint and
                                      not good else ""))

    print("workspace {}".format(WS))
    report("clone location", REPO, REPO == WS / "src" / "USD",
           "presets expect <workspace>/src/USD")
    report("USD_GEO_WS", os.environ.get("USD_GEO_WS"),
           bool(os.environ.get("USD_GEO_WS")), "unset; using default")
    print("tools")
    report("python", sys.version.split()[0], sys.version_info >= (3, 9))
    cmake = tool_version(["cmake", "--version"])
    cmake_ok = False
    if cmake:
        ver = tuple(int(x) for x in cmake.split()[-1].split(".")[:2])
        cmake_ok = ver >= CMAKE_MIN
    report("cmake", cmake, cmake_ok, ">= {}.{} required".format(*CMAKE_MIN))
    report("ninja", tool_version(["ninja", "--version"]),
           bool(shutil.which("ninja")))
    if is_windows():
        report("cl", shutil.which("cl"), bool(shutil.which("cl")),
               "run from a VS 2022 developer prompt")
    else:
        cc = os.environ.get("CXX") or ("clang++" if platform.system() ==
                                       "Darwin" else "g++")
        report("c++ compiler", tool_version([cc, "--version"]),
               bool(shutil.which(cc)))
    report("ccache", tool_version(["ccache", "--version"]),
           bool(shutil.which("ccache")), "optional, speeds up rebuilds",
           optional=True)
    report("git", tool_version(["git", "--version"]), bool(shutil.which("git")))
    tool = shutil.which("podman") or shutil.which("docker")
    report("podman/docker", tool, bool(tool), "needed for serve docker")
    report("npm", tool_version(["npm", "--version"]), bool(shutil.which("npm")),
           "needed for the web app")
    print("workspace contents")
    report("venv", VENV, venv_python(VENV).exists(),
           "geo.py bootstrap native --steps venv")
    report("emsdk " + EMSDK_VERSION, EMSDK,
           (EMSDK / "upstream" / "emscripten").is_dir(),
           "geo.py bootstrap wasm32 --steps emsdk")
    for target in TARGETS:
        d = deps_dir(target)
        have = [n for n, marker in (("oneTBB", "include/tbb"),
                                    ("OpenSubdiv", "include/opensubdiv"),
                                    ("SQLite", "include/sqlite3.h"),
                                    ("PROJ", "include/proj.h"))
                if (d / marker).exists()]
        report("deps/" + target, ", ".join(have) or None,
               len(have) >= 3, "geo.py bootstrap " + target)
    sys.exit(0 if ok else 1)


# --------------------------------------------------------------------------

def main():
    jobs = str(os.cpu_count() or 4)
    p = argparse.ArgumentParser(
        description="Local driver for the geospatial web prototype.",
        epilog="Workspace: {}".format(WS))
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("check", help="check tools and workspace layout")
    s.set_defaults(fn=cmd_check)

    s = sub.add_parser("bootstrap", help="build third-party deps for a target")
    s.add_argument("target", choices=TARGETS)
    s.add_argument("--steps", help="comma-separated subset of: " + "; ".join(
        "{}: {}".format(t, ",".join(n for n, _ in BOOTSTRAP_STEPS[t]))
        for t in TARGETS))
    s.add_argument("--clean", action="store_true",
                   help="delete the selected steps' build trees first, and "
                        "the venv if the venv step is selected (downloads "
                        "and emsdk are kept)")
    s.add_argument("--imaging", action="store_true",
                   help="wasm32: pass --usd-imaging to build_usd.py "
                        "(OpenSubdiv, GPU-free Hydra and usdImaging)")
    s.add_argument("-j", "--jobs", default=jobs)
    s.set_defaults(fn=cmd_bootstrap)

    s = sub.add_parser("build", help="configure, build and install a preset")
    s.add_argument("preset", choices=PRESETS)
    s.add_argument("--target", nargs="+", help="build these targets instead")
    s.add_argument("--reconfigure", action="store_true",
                   help="run CMake's configure step again")
    s.add_argument("--fresh", action="store_true",
                   help="configure from scratch, discarding the CMake cache")
    s.add_argument("-j", "--jobs", default=jobs)
    s.set_defaults(fn=cmd_build)

    s = sub.add_parser("test", help="run ctest for a preset, or the web "
                                    "app's Playwright tests (web)")
    s.add_argument("preset", choices=list(PRESETS) + ["web"])
    s.add_argument("-j", "--jobs", default=jobs)
    s.add_argument("--ui", action="store_true",
                   help="web: Playwright UI mode (interactive)")
    s.add_argument("--headed", action="store_true",
                   help="web: show the browser during the run")
    s.add_argument("ctest_args", nargs=argparse.REMAINDER,
                   help="passed to ctest or playwright after --")
    s.set_defaults(fn=cmd_test)

    s = sub.add_parser("reference", help="run the offline Python reference")
    s.add_argument("ref_args", nargs=argparse.REMAINDER)
    s.set_defaults(fn=cmd_reference)

    s = sub.add_parser("record", help="write the C++ placement of the test "
                                      "scene for the web app")
    s.add_argument("preset", nargs="?", default="native-debug",
                   choices=[p for p, t in PRESETS.items() if t == "native"])
    s.add_argument("record_args", nargs=argparse.REMAINDER,
                   help="after --: <stage> <outDir> [name=CRS ...]")
    s.set_defaults(fn=cmd_record)

    s = sub.add_parser("usdview", help="usdview of a native install, with the "
                                       "stage placed in an output CRS")
    s.add_argument("preset", nargs="?", default="native-debug",
                   choices=[p for p, t in PRESETS.items() if t == "native"])
    s.add_argument("--stage", default="",
                   help="default: testdata/scene/scene.usda")
    s.add_argument("--crs", default="EPSG:4979",
                   help="output CRS for USDGEOSPATIALIMAGING_OUTPUT_CRS; "
                        "'none' shows the unplaced stage")
    s.add_argument("--origin", default="",
                   help='display origin "x, y, z" in the output CRS '
                        "(default: the first anchor placed)")
    s.set_defaults(fn=cmd_usdview, usdview_args=[])

    s = sub.add_parser("serve", help="serve the web app with COOP/COEP")
    s.add_argument("mode", choices=("dev", "docker"))
    s.add_argument("--port", default="8080")
    s.set_defaults(fn=cmd_serve)

    # usdview: everything after "--" goes to usdview as is (argparse would
    # take it for the optional preset positional).
    argv = sys.argv[1:]
    passthrough = []
    if argv[:1] == ["usdview"] and "--" in argv:
        cut = argv.index("--")
        argv, passthrough = argv[:cut], argv[cut + 1:]
    args = p.parse_args(argv)
    if passthrough:
        args.usdview_args = passthrough
    args.fn(args)


if __name__ == "__main__":
    main()
