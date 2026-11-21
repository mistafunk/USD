import { createReadStream, cpSync, statSync } from "node:fs";
import { extname, join, normalize } from "node:path";
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
  ".avif": "image/avif", ".woff2": "font/woff2",
};

function arcgisAssets(): Plugin {
  let outDir = "dist";
  return {
    name: "arcgis-assets",
    configResolved(config) { outDir = config.build.outDir; },
    configureServer(server) {
      server.middlewares.use(ASSETS_URL, (req, res, next) => {
        const rel = normalize(decodeURIComponent((req.url ?? "/").split("?")[0]));
        const file = join(ASSETS_DIR, rel);
        if (!file.startsWith(ASSETS_DIR)) return next();
        try {
          if (!statSync(file).isFile()) return next();
        } catch {
          return next();
        }
        for (const [k, v] of Object.entries(isolation)) res.setHeader(k, v);
        res.setHeader("Content-Type", TYPES[extname(file)] ?? "application/octet-stream");
        createReadStream(file).pipe(res);
      });
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
  build: { target: "es2022" },
});
