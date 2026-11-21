// Hard-coded test geometry in local ENU metres (x east, y north, z up).

export interface LocalMesh {
  positions: Float32Array;
  normals: Float32Array;
  indices: Uint32Array;
}

/** Axis-aligned box standing on z = 0, centred on the origin in x and y. */
export function box(sx: number, sy: number, sz: number): LocalMesh {
  const x = sx / 2, y = sy / 2;
  // Each face: normal and its four corners, counter-clockwise from outside.
  const faces: [number[], number[][]][] = [
    [[1, 0, 0], [[x, -y, 0], [x, y, 0], [x, y, sz], [x, -y, sz]]],
    [[-1, 0, 0], [[-x, y, 0], [-x, -y, 0], [-x, -y, sz], [-x, y, sz]]],
    [[0, 1, 0], [[x, y, 0], [-x, y, 0], [-x, y, sz], [x, y, sz]]],
    [[0, -1, 0], [[-x, -y, 0], [x, -y, 0], [x, -y, sz], [-x, -y, sz]]],
    [[0, 0, 1], [[-x, -y, sz], [x, -y, sz], [x, y, sz], [-x, y, sz]]],
    [[0, 0, -1], [[-x, y, 0], [x, y, 0], [x, -y, 0], [-x, -y, 0]]],
  ];
  const positions: number[] = [], normals: number[] = [], indices: number[] = [];
  faces.forEach(([n, corners], f) => {
    for (const c of corners) {
      positions.push(...c);
      normals.push(...n);
    }
    const b = f * 4;
    indices.push(b, b + 1, b + 2, b, b + 2, b + 3);
  });
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    indices: new Uint32Array(indices),
  };
}
