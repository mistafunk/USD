import { defineConfig } from "@playwright/test";

// WebGL rendering. Default: the GPU, about ten times faster than software for
// SceneView (W0 global: 3 s vs 30 s to first settle on an RTX 2080), with
// Chromium's own software fallback when no GPU is usable.
// GEO_SOFTWARE_GL=1 forces SwiftShader, e.g. for reproducible CPU-only runs.
// Nothing here selects X11 or Wayland; headed runs use whatever Chromium picks.
const softwareGL = process.env.GEO_SOFTWARE_GL === "1";
const glArgs = softwareGL
  ? ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"]
  : ["--enable-gpu", "--ignore-gpu-blocklist", "--enable-unsafe-swiftshader"];

export default defineConfig({
  testDir: "tests",
  timeout: 120_000,
  reporter: [["list"], ["html", { open: "never" }]],
  use: {
    baseURL: "http://localhost:5173",
    viewport: { width: 1280, height: 800 },
    // Optional browser channel, e.g. GEO_BROWSER_CHANNEL=chrome for the
    // installed Google Chrome instead of Playwright's bundled Chromium.
    channel: process.env.GEO_BROWSER_CHANNEL || undefined,
    launchOptions: { args: glArgs },
  },
  webServer: {
    command: "npm run dev",
    url: "http://localhost:5173",
    reuseExistingServer: true,
  },
});
