import { defineConfig } from "@playwright/test";

// WebGL rendering. Default: the GPU, about ten times faster than software for
// SceneView (W0 global: 3 s vs 30 s to first settle on an RTX 2080), with
// Chromium's own software fallback when no GPU is usable.
// GEO_SOFTWARE_GL=1 forces SwiftShader, e.g. for reproducible CPU-only runs.
// Nothing here selects X11 or Wayland; headed runs use whatever Chromium picks.
const softwareGL = process.env.GEO_SOFTWARE_GL === "1";
// GEO_WASM_PRESET (geo.py test web --wasm-preset): another wasm32 build, on
// the dev server's port for it (vite.config.ts).
const port = (process.env.GEO_WASM_PRESET || "wasm32") === "wasm32" ? 5173 : 5174;
const glArgs = softwareGL
  ? ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"]
  : ["--enable-gpu", "--ignore-gpu-blocklist", "--enable-unsafe-swiftshader"];

export default defineConfig({
  testDir: "tests",
  timeout: 120_000,
  reporter: [["list"], ["html", { open: "never" }]],
  use: {
    baseURL: `http://localhost:${port}`,
    viewport: { width: 1280, height: 800 },
    // Optional browser channel, e.g. GEO_BROWSER_CHANNEL=chrome for the
    // installed Google Chrome instead of Playwright's bundled Chromium.
    channel: process.env.GEO_BROWSER_CHANNEL || undefined,
    launchOptions: { args: glArgs },
  },
  webServer: {
    command: "npm run dev",
    url: `http://localhost:${port}`,
    reuseExistingServer: true,
  },
});
