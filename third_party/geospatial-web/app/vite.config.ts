import { createReadStream, cpSync, existsSync, statSync } from "node:fs";
import type { IncomingMessage, ServerResponse } from "node:http";
import { extname, join, normalize, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, type Plugin } from "vite";

// OpenUSD's WASM build uses pthreads, which need SharedArrayBuffer, which needs
// a cross-origin isolated page. Same headers as tools/nginx.conf.
const isolation = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};

// The SDK loads its assets from js.arcgis.com by default. Under COEP
// require-corp the worker script from there is blocked (no CORP header), so
// serve the assets from our own origin; src/main.ts sets esriConfig.assetsPath.
const ASSETS_URL = "/arcgis-assets";
const ASSETS_DIR = fileURLToPath(new URL("node_modules/@arcgis/core/assets", import.meta.url));
const TYPES: Record<string, string> = {
  ".js": "text/javascript", ".json": "application/json", ".css": "text/css",
  ".wasm": "application/wasm", ".svg": "image/svg+xml", ".png": "image/png",
  ".avif": "image/avif", ".woff2": "font/woff2", ".usda": "text/plain",
};

// The OpenUSD module and test fixtures, installed by geo.py build wasm32 to
// <workspace>/install/wasm32/share/geospatial-web and served at /usd, as
// nginx does. Workspace: USD_GEO_WS, else five levels above this app
// (<workspace>/src/USD/third_party/geospatial-web/app).
const USD_URL = "/usd";
const WORKSPACE = process.env.USD_GEO_WS ||
  fileURLToPath(new URL("../../../../..", import.meta.url));
const USD_DIR = resolve(WORKSPACE, "install", "wasm32", "share", "geospatial-web");

/**
 * Serves files under dir with the isolation headers, and a precompressed
 * <file>.gz if there is one and the client accepts gzip (like nginx
 * gzip_static); falls through otherwise.
 */
function serveDir(dir: string) {
  return (req: IncomingMessage, res: ServerResponse, next: () => void) => {
    const rel = normalize(decodeURIComponent((req.url ?? "/").split("?")[0]));
    const file = join(dir, rel);
    if (!file.startsWith(dir)) return next();
    try {
      if (!statSync(file).isFile()) return next();
    } catch {
      return next();
    }
    for (const [k, v] of Object.entries(isolation)) res.setHeader(k, v);
    res.setHeader("Content-Type", TYPES[extname(file)] ?? "application/octet-stream");
    const gz = file + ".gz";
    if (/\bgzip\b/.test(String(req.headers["accept-encoding"] ?? "")) && existsSync(gz)) {
      res.setHeader("Content-Encoding", "gzip");
      res.setHeader("Vary", "Accept-Encoding");
      createReadStream(gz).pipe(res);
      return;
    }
    createReadStream(file).pipe(res);
  };
}

function arcgisAssets(): Plugin {
  let outDir = "dist";
  return {
    name: "arcgis-assets",
    configResolved(config) { outDir = config.build.outDir; },
    configureServer(server) {
      server.middlewares.use(ASSETS_URL, serveDir(ASSETS_DIR));
      server.middlewares.use(USD_URL, serveDir(USD_DIR));
    },
    closeBundle() {
      cpSync(ASSETS_DIR, join(outDir, ASSETS_URL), { recursive: true });
    },
  };
}

export default defineConfig({
  plugins: [arcgisAssets()],
  define: { __ARCGIS_ASSETS__: JSON.stringify(ASSETS_URL) },
  // Pre-bundle the SDK modules we import, so Vite does not discover them at
  // runtime and reload the page (which aborts in-flight requests in tests).
  optimizeDeps: {
    include: [
      "@arcgis/core/Basemap.js",
      "@arcgis/core/Camera.js",
      "@arcgis/core/Ground.js",
      "@arcgis/core/Map.js",
      "@arcgis/core/config.js",
      "@arcgis/core/core/reactiveUtils.js",
      "@arcgis/core/geometry/SpatialReference.js",
      "@arcgis/core/geometry/support/webMercatorUtils.js",
      "@arcgis/core/layers/TileLayer.js",
      "@arcgis/core/views/3d/webgl.js",
      "@arcgis/core/views/3d/webgl/ManagedFBO.js",
      "@arcgis/core/views/3d/webgl/RenderNode.js",
      "@arcgis/core/views/SceneView.js",
    ],
  },
  server: { port: 5173, strictPort: true, headers: isolation },
  preview: { port: 4173, strictPort: true, headers: isolation },
  build: {
    target: "es2022",
    rollupOptions: {
      input: {
        main: fileURLToPath(new URL("index.html", import.meta.url)),
        c4: fileURLToPath(new URL("c4.html", import.meta.url)),
      },
    },
  },
  worker: { format: "es" },
});
