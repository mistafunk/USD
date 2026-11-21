// Minimal WebGL2 mesh pass for a SceneView RenderNode.
//
// Each mesh is drawn relative to its own render origin, which is exactly
// representable in float32. All matrix products happen in Float64; only the
// final per-draw modelView matrix is cast to float32. This is the layout the
// C++ scene index observer will produce (one origin and matrix per 3DOL).

import RenderNode, { type RenderNodeProperties } from "@arcgis/core/views/3d/webgl/RenderNode.js";
import type ManagedFBO from "@arcgis/core/views/3d/webgl/ManagedFBO.js";
import type SceneView from "@arcgis/core/views/SceneView.js";
import { type Mat4, multiply, normalMatrix3, translation } from "./mat4";

export interface PlacedMesh {
  name: string;
  /** Render-space origin; float32-exact. */
  origin: Float32Array;
  /** Mesh-local to render space, relative to `origin` (column-major, Float64). */
  matrix: Float64Array;
  positions: Float32Array;
  normals: Float32Array;
  indices: Uint32Array;
  color: [number, number, number];
}

export interface PassStats {
  frames: number;
  near: number;
  meshes: number;
  triangles: number;
}

const VERTEX_SHADER = `#version 300 es
in vec3 aPosition;
in vec3 aNormal;
uniform mat4 uModelView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
out vec3 vNormal;
void main() {
  vNormal = uNormalMatrix * aNormal;
  gl_Position = uProjection * uModelView * vec4(aPosition, 1.0);
}`;

const FRAGMENT_SHADER = `#version 300 es
precision highp float;
in vec3 vNormal;
uniform vec3 uColor;
uniform vec3 uLightDir;   // view space, towards the light
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragEmissive;
void main() {
  float lambert = max(dot(normalize(vNormal), uLightDir), 0.0);
  fragColor = vec4(uColor * (uAmbient + uDiffuse * lambert), 1.0);
  fragEmissive = vec4(0.0);
}`;

interface GpuMesh {
  mesh: PlacedMesh;
  vao: WebGLVertexArrayObject;
  buffers: WebGLBuffer[];
  count: number;
}

export class MeshPass {
  readonly stats: PassStats = { frames: 0, near: 0, meshes: 0, triangles: 0 };
  private node: RenderNode;
  private gl: WebGL2RenderingContext | null = null;
  private program: WebGLProgram | null = null;
  private uniforms: Record<string, WebGLUniformLocation | null> = {};
  private pending: PlacedMesh[] = [];
  private gpu: GpuMesh[] = [];

  constructor(view: SceneView) {
    const pass = this;
    const Node = RenderNode.createSubclass({
      render(this: RenderNode): ManagedFBO | null | undefined {
        return pass.render(this);
      },
    }) as unknown as new (props: RenderNodeProperties) => RenderNode;
    // consumes/produces must be constructor properties: the SDK picks the
    // target framebuffer by `produces`, and class-definition values are lost.
    this.node = new Node({
      view,
      consumes: { required: ["opaque-color"] },
      produces: "opaque-color",
    });
  }

  setMeshes(meshes: PlacedMesh[]): void {
    this.pending = meshes;
    this.node.requestRender();
  }

  destroy(): void {
    this.releaseMeshes();
    this.gl?.deleteProgram(this.program);
    (this.node as unknown as { destroy(): void }).destroy();
  }

  private render(node: RenderNode): ManagedFBO | null | undefined {
    const gl = node.gl;
    if (!this.program) this.init(gl);
    if (this.pending.length || !this.gpu.length) this.upload(gl);

    const output = node.bindRenderTarget();
    const camera = node.camera;
    const view = camera.viewMatrix as Mat4;

    gl.useProgram(this.program);
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);

    gl.uniformMatrix4fv(this.uniforms.uProjection, false,
      new Float32Array(camera.projectionMatrix));
    const sun = node.sunLight;
    // Sun direction is in render space; rotate it into view space.
    const d = sun.direction;
    const lightDir = [
      view[0] * d[0] + view[4] * d[1] + view[8] * d[2],
      view[1] * d[0] + view[5] * d[1] + view[9] * d[2],
      view[2] * d[0] + view[6] * d[1] + view[10] * d[2],
    ];
    const len = Math.hypot(lightDir[0], lightDir[1], lightDir[2]) || 1;
    gl.uniform3f(this.uniforms.uLightDir,
      lightDir[0] / len, lightDir[1] / len, lightDir[2] / len);
    gl.uniform3fv(this.uniforms.uAmbient,
      sun.ambient.color.map((c) => c * sun.ambient.intensity));
    gl.uniform3fv(this.uniforms.uDiffuse,
      sun.diffuse.color.map((c) => c * sun.diffuse.intensity));

    let triangles = 0;
    for (const g of this.gpu) {
      const { origin, matrix, color } = g.mesh;
      // view * translate(origin) * matrix, all in Float64.
      const modelView = multiply(multiply(view, translation(origin)), matrix);
      gl.uniformMatrix4fv(this.uniforms.uModelView, false,
        new Float32Array(modelView));
      gl.uniformMatrix3fv(this.uniforms.uNormalMatrix, false,
        new Float32Array(normalMatrix3(modelView)));
      gl.uniform3fv(this.uniforms.uColor, color);
      gl.bindVertexArray(g.vao);
      gl.drawElements(gl.TRIANGLES, g.count, gl.UNSIGNED_INT, 0);
      triangles += g.count / 3;
    }

    gl.bindVertexArray(null);
    gl.useProgram(null);
    node.resetWebGLState();

    this.stats.frames++;
    this.stats.near = camera.near;
    this.stats.meshes = this.gpu.length;
    this.stats.triangles = triangles;
    return output;
  }

  private init(gl: WebGL2RenderingContext): void {
    this.gl = gl;
    const compile = (type: number, src: string) => {
      const s = gl.createShader(type)!;
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        throw new Error("shader: " + gl.getShaderInfoLog(s));
      }
      return s;
    };
    const p = gl.createProgram()!;
    gl.attachShader(p, compile(gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(p, compile(gl.FRAGMENT_SHADER, FRAGMENT_SHADER));
    gl.bindAttribLocation(p, 0, "aPosition");
    gl.bindAttribLocation(p, 1, "aNormal");
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      throw new Error("program: " + gl.getProgramInfoLog(p));
    }
    this.program = p;
    for (const name of ["uModelView", "uProjection", "uNormalMatrix", "uColor",
                        "uLightDir", "uAmbient", "uDiffuse"]) {
      this.uniforms[name] = gl.getUniformLocation(p, name);
    }
  }

  private upload(gl: WebGL2RenderingContext): void {
    if (!this.pending.length) return;
    this.releaseMeshes();
    for (const mesh of this.pending) {
      const vao = gl.createVertexArray()!;
      gl.bindVertexArray(vao);
      const buffers: WebGLBuffer[] = [];
      const attrib = (index: number, data: Float32Array) => {
        const b = gl.createBuffer()!;
        gl.bindBuffer(gl.ARRAY_BUFFER, b);
        gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
        gl.enableVertexAttribArray(index);
        gl.vertexAttribPointer(index, 3, gl.FLOAT, false, 0, 0);
        buffers.push(b);
      };
      attrib(0, mesh.positions);
      attrib(1, mesh.normals);
      const ib = gl.createBuffer()!;
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);
      buffers.push(ib);
      gl.bindVertexArray(null);
      this.gpu.push({ mesh, vao, buffers, count: mesh.indices.length });
    }
    this.pending = [];
  }

  private releaseMeshes(): void {
    const gl = this.gl;
    if (!gl) return;
    for (const g of this.gpu) {
      gl.deleteVertexArray(g.vao);
      g.buffers.forEach((b) => gl.deleteBuffer(b));
    }
    this.gpu = [];
  }
}
