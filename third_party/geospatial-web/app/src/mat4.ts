// Column-major 4x4 matrices in double precision (WebGL layout: translation in
// elements 12..14). Deliberately plain number arrays, never Float32Array.

export type Mat4 = ArrayLike<number>;

export function multiply(a: Mat4, b: Mat4): Float64Array {
  const out = new Float64Array(16);
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      out[c * 4 + r] =
        a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] +
        a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    }
  }
  return out;
}

export function translation(t: ArrayLike<number>): Float64Array {
  const out = identity();
  out[12] = t[0];
  out[13] = t[1];
  out[14] = t[2];
  return out;
}

export function identity(): Float64Array {
  const out = new Float64Array(16);
  out[0] = out[5] = out[10] = out[15] = 1;
  return out;
}

/**
 * Inverse transpose of the upper-left 3x3, column-major. With columns c0, c1,
 * c2 it is [c1 x c2, c2 x c0, c0 x c1] / det.
 */
export function normalMatrix3(m: Mat4): Float64Array {
  const c0 = [m[0], m[1], m[2]];
  const c1 = [m[4], m[5], m[6]];
  const c2 = [m[8], m[9], m[10]];
  const cross = (a: number[], b: number[]) => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
  const n0 = cross(c1, c2), n1 = cross(c2, c0), n2 = cross(c0, c1);
  const det = c0[0] * n0[0] + c0[1] * n0[1] + c0[2] * n0[2];
  const inv = det ? 1 / det : 0;
  return new Float64Array([...n0, ...n1, ...n2].map((v) => v * inv));
}
