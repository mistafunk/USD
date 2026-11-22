// Loading status for the panel: the current phase, download progress per
// file and a log of steps, USD messages and errors, so a failed or slow load
// (e.g. a large glb from a feature service) can be diagnosed in the page.

export type LogLevel = "info" | "warn" | "error" | "usd";

export interface LogEntry {
  /** Seconds since the load started. */
  t: number;
  level: LogLevel;
  text: string;
}

export class LoadStatus {
  phase = "starting";
  done = false;
  failed = false;
  readonly log: LogEntry[] = [];
  /** Downloads in flight or done: label -> bytes loaded, total (0 if unknown). */
  readonly files = new Map<string, { loaded: number; total: number }>();
  private readonly start = performance.now();
  private readonly listeners: (() => void)[] = [];

  private elapsed(): number {
    return (performance.now() - this.start) / 1000;
  }

  onChange(listener: () => void) {
    this.listeners.push(listener);
  }

  private changed() {
    for (const l of this.listeners) l();
  }

  /** Starts a phase (shown in the bar and logged). */
  step(phase: string) {
    this.phase = phase;
    this.add("info", phase);
  }

  add(level: LogLevel, text: string) {
    this.log.push({ t: this.elapsed(), level, text });
    if (this.log.length > 400) this.log.splice(0, this.log.length - 400);
    if (level === "error") console.error(text);
    this.changed();
  }

  bytes(label: string, loaded: number, total: number) {
    this.files.set(label, { loaded, total });
    this.changed();
  }

  finish(summary: string) {
    this.done = true;
    this.phase = `${summary} (${this.elapsed().toFixed(1)} s)`;
    this.add("info", this.phase);
  }

  fail(error: unknown) {
    this.failed = true;
    this.done = true;
    const text = String((error as Error)?.message ?? error);
    this.phase = `failed: ${text}`;
    this.add("error", text);
  }

  /** Overall download progress: loaded, total bytes (total 0 if unknown). */
  downloads(): { loaded: number; total: number; active: number } {
    let loaded = 0, total = 0, active = 0;
    for (const f of this.files.values()) {
      loaded += f.loaded;
      total += f.total;
      if (!f.total || f.loaded < f.total) active++;
    }
    return { loaded, total, active };
  }
}

export function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / 1024 / 1024).toFixed(1)} MiB`;
}
