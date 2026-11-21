"""Offline reference for the geospatial web prototype (plan option D).

Evaluates the geospatial CRS proposal's model placement with usd-core and
pyproj, independently of the C++ library, and writes recorded data in the
layout the C3 scene index observer will produce (docs/RECORDED_FORMAT.md).

    reference.py fixtures            write testdata/w1/*.usda
    reference.py export [stage]      write app/public/recorded/
    reference.py check               consistency checks on the export

Placement (proposal, "Geospatial model attitude and stage axes" and
"Evaluation"), for a point x in the anchor's model frame, in stage units:

    v = metersPerUnit * axisMap(upAxis) * x           model ENU metres
    G = ECEF_src(anchor) + B_enu * R(h, p, r) * v     source-datum geocentric
    Q = T_src->out(G)                                  output CRS

B_enu is the source ellipsoid's east/north/up basis at the anchor. Descendant
transforms D act before placement, F(x * D). The anchor's own ordinary
transform A (a post-placement adjustment) is not supported here yet.
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
import pyproj
from pyproj import CRS, Transformer
from pxr import Gf, Sdf, Usd, UsdGeom

HERE = Path(__file__).resolve().parent
GEO_DIR = HERE.parent
W1_DIR = GEO_DIR / "testdata" / "w1"
RECORDED_DIR = GEO_DIR / "app" / "public" / "recorded"

# Property and schema names, pinned to the proposal at 4120379f (D3, D14).
API_BINDING = "GeospatialCRSBindingAPI"
TYPE_CRS = "GeospatialCRS"
ATTR_WKT = "crs:wkt"
ATTR_POSITION = "crs:position"
ATTR_ORIENTATION = "crs:orientation"

OUTPUTS = {
    # name: (output CRS, frame). "enu": matrices map into metres in the
    # output datum's ENU frame at the anchor (global SceneView, D13).
    # "crs": matrices map into output CRS units (local SceneView).
    "global": ("EPSG:4979", "enu"),
    "local-3857": ("EPSG:3857", "crs"),
}


class Unsupported(Exception):
    """Placement the reference (or engine) cannot do; reported, not drawn."""


class Invalid(Exception):
    """Broken authored data, e.g. a direct binding without crs:wkt."""


# ---------------------------------------------------------------------------
# Geodesy on an ellipsoid (closed form, so ENU/ECEF do not depend on PROJ)

class Ellipsoid:
    def __init__(self, a, inv_f):
        self.a = a
        self.f = 1.0 / inv_f if inv_f else 0.0
        self.e2 = self.f * (2.0 - self.f)

    @classmethod
    def of(cls, crs):
        e = crs.ellipsoid
        return cls(e.semi_major_metre, e.inverse_flattening)

    def to_ecef(self, lon, lat, h):
        lon, lat = math.radians(lon), math.radians(lat)
        n = self.a / math.sqrt(1.0 - self.e2 * math.sin(lat) ** 2)
        return np.array([
            (n + h) * math.cos(lat) * math.cos(lon),
            (n + h) * math.cos(lat) * math.sin(lon),
            (n * (1.0 - self.e2) + h) * math.sin(lat),
        ])

    def from_ecef(self, x, y, z):
        lon = math.atan2(y, x)
        p = math.hypot(x, y)
        lat = math.atan2(z, p * (1.0 - self.e2))
        for _ in range(8):
            n = self.a / math.sqrt(1.0 - self.e2 * math.sin(lat) ** 2)
            h = p / math.cos(lat) - n
            lat = math.atan2(z, p * (1.0 - self.e2 * n / (n + h)))
        n = self.a / math.sqrt(1.0 - self.e2 * math.sin(lat) ** 2)
        h = p / math.cos(lat) - n
        return math.degrees(lon), math.degrees(lat), h


def enu_basis(lon, lat):
    """Columns east, north, up (ellipsoid normal) in ECEF."""
    lon, lat = math.radians(lon), math.radians(lat)
    sl, cl, sp, cp = math.sin(lon), math.cos(lon), math.sin(lat), math.cos(lat)
    return np.array([
        [-sl, -sp * cl, cp * cl],
        [cl, -sp * sl, cp * sl],
        [0.0, cp, sp],
    ])


def rotation(axis, degrees):
    """Column-vector rotation about a unit axis (right-hand rule)."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    x, y, z = axis
    return np.array([
        [c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
        [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
        [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)],
    ])


def attitude(heading, pitch, roll):
    """Proposal: Core row-vector R_N(roll) * R_E(pitch) * R_U(-heading),
    i.e. column-vector R_U(-heading) @ R_E(pitch) @ R_N(roll), in ENU."""
    return (rotation((0, 0, 1), -heading) @ rotation((1, 0, 0), pitch)
            @ rotation((0, 1, 0), roll))


def axis_map(up_axis):
    """Stage (x, y, z) to model (E, N, U)."""
    if up_axis == "Z":
        return np.eye(3)
    return np.array([[1.0, 0, 0], [0, 0, -1.0], [0, 1.0, 0]])  # (x, -z, y)


# ---------------------------------------------------------------------------
# CRS handling (pyproj is the engine here)

SUPPORTED_DIRECTIONS = {
    "projected": ["east", "north", "up"],
    "geographic": ["north", "east", "up"],
    "geocentric": ["geocentricX", "geocentricY", "geocentricZ"],
}


def crs_kind(crs):
    if crs.is_geocentric:
        return "geocentric"
    if crs.is_projected:
        return "projected"
    if crs.is_geographic:
        return "geographic"
    raise Unsupported("unsupported CRS type: " + crs.type_name)


def check_source_crs(crs):
    kind = crs_kind(crs)
    dirs = [a.direction for a in crs.axis_info]
    if len(dirs) != 3:
        raise Unsupported(
            "2D CRS: height needs the EGM2008 default vertical reference, "
            "which needs a geoid grid this reference does not ship")
    if sorted(dirs) != sorted(SUPPORTED_DIRECTIONS[kind]):
        raise Unsupported("unsupported axis directions {}".format(dirs))
    return kind


class Placement:
    """One directly bound anchor, resolved for one output CRS."""

    def __init__(self, anchor, stage_mpu, up_axis, out_crs, frame):
        self.path = str(anchor.GetPath())
        wkt_attr = anchor.GetAttribute(ATTR_WKT)
        wkt = wkt_attr.Get() if wkt_attr else None
        if not wkt:
            raise Invalid("direct binding without a crs:wkt definition "
                          "(no fallback to an ancestor CRS)")
        try:
            self.src = CRS.from_wkt(str(wkt))
        except pyproj.exceptions.CRSError as e:
            raise Invalid("crs:wkt does not parse: {}".format(e))
        self.src_name = self.src.name
        check_source_crs(self.src)

        pos_attr = anchor.GetAttribute(ATTR_POSITION)
        if not pos_attr or not pos_attr.HasAuthoredValue():
            raise Invalid("missing crs:position")
        pos = pos_attr.Get()
        if pos is None or not all(math.isfinite(c) for c in pos):
            raise Invalid("crs:position is blocked or not finite")
        self.position = [float(c) for c in pos]
        ori_attr = anchor.GetAttribute(ATTR_ORIENTATION)
        ori = ori_attr.Get() if ori_attr and ori_attr.HasAuthoredValue() \
            else (0.0, 0.0, 0.0)
        if ori is None or not all(math.isfinite(c) for c in ori):
            raise Invalid("crs:orientation is blocked or not finite")
        self.orientation = [float(c) for c in ori]

        own = UsdGeom.Xformable(anchor).GetLocalTransformation()
        if own != Gf.Matrix4d(1.0):
            raise Unsupported("anchor ordinary transform A (post-placement "
                              "adjustment) is not supported by the reference yet")

        # Anchor in the source datum's geographic 3D CRS.
        self.src_geo = CRS(self.src.geodetic_crs).to_3d()
        self.src_ell = Ellipsoid.of(self.src_geo)
        to_geo = Transformer.from_crs(self.src, self.src_geo, always_xy=True)
        lon, lat, h = to_geo.transform(*self.position)
        if kind_is_geocentric_pole(self.src, self.position):
            raise Unsupported("model attitude undefined at geocentric X = Y = 0")
        self.anchor_geo = (lon, lat, h)
        self.origin_ecef = self.src_ell.to_ecef(lon, lat, h)
        self.basis = enu_basis(lon, lat)
        self.model = (stage_mpu * self.basis @ attitude(*self.orientation)
                      @ axis_map(up_axis))

        # Source geographic 3D -> output CRS (no ballpark operations: a
        # transform without a real operation is unsupported, not guessed).
        self.out_crs = CRS(out_crs)
        if not self.out_crs.is_geographic:
            self.out_crs = self.out_crs.to_3d()
        self.frame = frame
        try:
            self.to_out = Transformer.from_crs(
                self.src_geo, self.out_crs, always_xy=True,
                allow_ballpark=False)
        except pyproj.exceptions.ProjError as e:
            raise Unsupported("no coordinate operation: {}".format(e))
        self.operation = self.to_out.description
        acc = self.to_out.accuracy
        self.accuracy = acc if acc is not None and acc >= 0 else None

        self.origin_out = self._to_output(self.origin_ecef)
        if not all(math.isfinite(c) for c in self.origin_out):
            raise Unsupported("coordinate operation failed at the anchor")
        if frame == "enu":
            out_geo = CRS(self.out_crs.geodetic_crs).to_3d()
            self.out_ell = Ellipsoid.of(out_geo)
            lon, lat, h = self.origin_out
            self.out_origin_ecef = self.out_ell.to_ecef(lon, lat, h)
            self.out_basis = enu_basis(lon, lat)
        self.stage_mpu = stage_mpu
        self.jacobian = self._jacobian()

    def _to_output(self, ecef):
        lon, lat, h = self.src_ell.from_ecef(*ecef)
        return np.array(self.to_out.transform(lon, lat, h))

    def exact(self, p):
        """Exact point map of model-frame point p (stage units) into the
        matrix frame: ENU metres at the output anchor, or output CRS units
        relative to the output anchor."""
        q = self._to_output(self.origin_ecef + self.model @ np.asarray(p))
        if self.frame == "enu":
            lon, lat, h = q
            d = self.out_ell.to_ecef(lon, lat, h) - self.out_origin_ecef
            return self.out_basis.T @ d
        return q - self.origin_out

    def _jacobian(self):
        step = 1.0 / self.stage_mpu  # one metre in stage units
        cols = []
        for i in range(3):
            e = np.zeros(3)
            e[i] = step
            cols.append((self.exact(e) - self.exact(-e)) / (2.0 * step))
        return np.column_stack(cols)


def kind_is_geocentric_pole(crs, position):
    return crs.is_geocentric and position[0] == 0.0 and position[1] == 0.0


# ---------------------------------------------------------------------------
# Stage traversal

def applied_schemas(prim):
    names = set(prim.GetAppliedSchemas())
    op = prim.GetMetadata("apiSchemas")
    if op:
        names.update(op.GetAddedOrExplicitItems())
    return names


def nearest_anchor(prim):
    p = prim
    while p and not p.IsPseudoRoot():
        if API_BINDING in applied_schemas(p):
            return p
        p = p.GetParent()
    return None


def column_major(m):
    """Gf.Matrix4d (row-vector) -> numpy 4x4 column-vector matrix."""
    return np.array([[m[r][c] for r in range(4)] for c in range(4)])


def mesh_triangles(mesh):
    """Flat-shaded, de-indexed triangles in mesh-local coordinates."""
    points = np.array(mesh.GetPointsAttr().Get(), dtype=np.float64)
    counts = mesh.GetFaceVertexCountsAttr().Get()
    indices = mesh.GetFaceVertexIndicesAttr().Get()
    left = mesh.GetOrientationAttr().Get() == UsdGeom.Tokens.leftHanded
    tris = []
    i = 0
    for n in counts:
        face = list(indices[i:i + n])
        i += n
        if left:
            face.reverse()
        for k in range(1, n - 1):
            tris.append((face[0], face[k], face[k + 1]))
    pos, nrm = [], []
    for a, b, c in tris:
        pa, pb, pc = points[a], points[b], points[c]
        n = np.cross(pb - pa, pc - pa)
        n /= np.linalg.norm(n) or 1.0
        pos.extend([pa, pb, pc])
        nrm.extend([n, n, n])
    return np.array(pos), np.array(nrm)


def display_color(mesh):
    pv = UsdGeom.PrimvarsAPI(mesh.GetPrim()).GetPrimvar("displayColor")
    value = pv.Get() if pv else None
    if value:
        return [float(c) for c in value[0]]
    return [0.8, 0.8, 0.8]


# ---------------------------------------------------------------------------
# export

def export(stage_path, out_dir):
    stage = Usd.Stage.Open(str(stage_path))
    mpu = UsdGeom.GetStageMetersPerUnit(stage)
    up = UsdGeom.GetStageUpAxis(stage)
    cache = UsdGeom.XformCache()

    meshes = [UsdGeom.Mesh(p) for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
    blob = bytearray()
    buffers = {}  # mesh path -> buffer views (shared by all outputs)
    for mesh in meshes:
        pos, nrm = mesh_triangles(mesh)
        views = {}
        for name, data, fmt in (("points", pos, "f"), ("normals", nrm, "f"),
                                ("indices", np.arange(len(pos)), "I")):
            flat = np.asarray(data).reshape(-1)
            views[name] = {"byteOffset": len(blob),
                           "count": len(flat) // (1 if name == "indices" else 3)}
            blob += struct.pack("<{}{}".format(len(flat), fmt),
                                *(float(v) if fmt == "f" else int(v) for v in flat))
        buffers[str(mesh.GetPath())] = views

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "buffers.bin").write_bytes(bytes(blob))

    for out_name, (out_crs, frame) in OUTPUTS.items():
        anchors, records = {}, []
        for mesh in meshes:
            path = str(mesh.GetPath())
            anchor = nearest_anchor(mesh.GetPrim())
            if anchor is None:
                records.append({"path": path, "anchor": None,
                                "status": "unplaced",
                                "reason": "no CRS binding on the mesh or an ancestor"})
                continue
            apath = str(anchor.GetPath())
            if apath not in anchors:
                anchors[apath] = resolve_anchor(anchor, mpu, up, out_crs, frame)
            a = anchors[apath]
            rec = {"path": path, "anchor": apath, "status": a["status"],
                   "color": display_color(mesh), **buffers[path]}
            if a["status"] == "ok":
                pl = a["_p"]
                d, _ = cache.ComputeRelativeTransform(mesh.GetPrim(), anchor)
                d = column_major(d)
                j = np.eye(4)
                j[:3, :3] = pl.jacobian
                rec["matrix"] = list((j @ d).T.reshape(-1))  # column-major
                pts = np.array(mesh.GetPointsAttr().Get(), dtype=np.float64)
                local = (d[:3, :3] @ pts.T).T + d[:3, 3]
                err = max(float(np.linalg.norm(pl.exact(p) - pl.jacobian @ p))
                          for p in local)
                rec["linearizationError"] = err
            records.append(rec)

        manifest = {
            "format": "geospatial-web-recorded",
            "version": 1,
            "stage": stage_path.name,
            "output": {"name": out_name, "crs": out_crs,
                       "wkt": CRS(out_crs).to_wkt("WKT2_2019"), "frame": frame},
            "engine": {"name": "pyproj", "version": pyproj.__version__,
                       "proj": pyproj.proj_version_str},
            "stageMetersPerUnit": mpu,
            "stageUpAxis": up,
            "buffers": "buffers.bin",
            "anchors": [{k: v for k, v in a.items() if not k.startswith("_")}
                        for a in anchors.values()],
            "meshes": records,
        }
        path = out_dir / "manifest-{}.json".format(out_name)
        path.write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
        print("wrote {} ({} meshes)".format(path, len(records)))


def resolve_anchor(anchor, mpu, up, out_crs, frame):
    rec = {"path": str(anchor.GetPath())}
    try:
        pl = Placement(anchor, mpu, up, out_crs, frame)
    except Invalid as e:
        return {**rec, "status": "error", "reason": str(e)}
    except Unsupported as e:
        return {**rec, "status": "unsupported", "reason": str(e)}
    return {**rec, "status": "ok", "sourceCrs": pl.src_name,
            "position": pl.position, "orientation": pl.orientation,
            "origin": [float(c) for c in pl.origin_out],
            "operation": pl.operation, "accuracy": pl.accuracy, "_p": pl}


# ---------------------------------------------------------------------------
# fixtures

SITE_A_LONLAT = (-117.1955, 34.0565)   # Esri campus, Redlands (approximate)
SITE_A_HEADING = 30.0
BUILDING = (40.0, 20.0, 12.0)          # x, y, z extents in metres (Z-up)
WING_OFFSET = (30.0, 5.0, 0.0)
WING = (10.0, 10.0, 6.0)
MAST = (1.0, 1.0, 25.0)


def box_mesh(stage, path, size, color):
    sx, sy, sz = size[0] / 2, size[1] / 2, size[2]
    pts = [(-sx, -sy, 0), (sx, -sy, 0), (sx, sy, 0), (-sx, sy, 0),
           (-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5),
             (2, 3, 7, 6), (3, 0, 4, 7)]
    mesh = UsdGeom.Mesh.Define(stage, path)
    mesh.CreatePointsAttr([Gf.Vec3f(*p) for p in pts])
    mesh.CreateFaceVertexCountsAttr([4] * len(faces))
    mesh.CreateFaceVertexIndicesAttr([i for f in faces for i in f])
    mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
    mesh.CreateDisplayColorAttr([Gf.Vec3f(*color)])
    mesh.CreateExtentAttr([Gf.Vec3f(-sx, -sy, 0), Gf.Vec3f(sx, sy, sz)])
    return mesh


def new_stage(path):
    stage = Usd.Stage.CreateNew(str(path))
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    return stage


def bind(prim, crs_ref, position, orientation=None):
    prim.AddAppliedSchema(API_BINDING)
    prim.GetReferences().AddReference("./crs_library.usda", crs_ref)
    prim.CreateAttribute(ATTR_POSITION, Sdf.ValueTypeNames.Double3,
                         custom=False).Set(Gf.Vec3d(*position))
    if orientation is not None:
        prim.CreateAttribute(ATTR_ORIENTATION, Sdf.ValueTypeNames.Double3,
                             custom=False).Set(Gf.Vec3d(*orientation))


def fixtures():
    W1_DIR.mkdir(parents=True, exist_ok=True)
    utm3d = CRS("EPSG:32611").to_3d()
    geo3d = CRS("EPSG:4979")

    lib = new_stage(W1_DIR / "crs_library.usda")
    lib.SetDefaultPrim(lib.DefinePrim("/CRS"))
    for name, crs in (("WGS84_UTM11N_3D", utm3d), ("WGS84_Geographic3D", geo3d)):
        prim = lib.DefinePrim("/CRS/" + name, TYPE_CRS)
        prim.CreateAttribute(ATTR_WKT, Sdf.ValueTypeNames.Token, custom=False,
                             variability=Sdf.VariabilityUniform).Set(
                                 crs.to_wkt("WKT2_2019"))
    lib.GetRootLayer().Save()

    # Site A: building in UTM 11N (3D, ellipsoidal height), heading 30.
    e, n = Transformer.from_crs("EPSG:4326", "EPSG:32611", always_xy=True) \
        .transform(*SITE_A_LONLAT)
    pos_a = (round(e, 2), round(n, 2), 0.0)
    site_a = new_stage(W1_DIR / "siteA.usda")
    a = UsdGeom.Xform.Define(site_a, "/SiteA").GetPrim()
    site_a.SetDefaultPrim(a)
    bind(a, "/CRS/WGS84_UTM11N_3D", pos_a, (SITE_A_HEADING, 0.0, 0.0))
    box_mesh(site_a, "/SiteA/Building", BUILDING, (0.85, 0.45, 0.15))
    wing = UsdGeom.Xform.Define(site_a, "/SiteA/Wing")
    wing.AddTranslateOp().Set(Gf.Vec3d(*WING_OFFSET))
    box_mesh(site_a, "/SiteA/Wing/Box", WING, (0.9, 0.7, 0.3))
    site_a.GetRootLayer().Save()

    # Site B: mast in geographic 3D, standing on Site A's building corner
    # (+x, +y, ground). The corner comes from PROJ's topocentric conversion,
    # an implementation independent of this script's ENU basis.
    lon_a, lat_a, h_a = Transformer.from_crs(
        utm3d, geo3d, always_xy=True).transform(*pos_a)
    corner = attitude(SITE_A_HEADING, 0, 0) @ np.array(
        [BUILDING[0] / 2, BUILDING[1] / 2, 0.0])
    topo = Transformer.from_pipeline(
        "+proj=pipeline +step +inv +proj=topocentric +ellps=WGS84 "
        "+lon_0={} +lat_0={} +h_0={} +step +inv +proj=cart +ellps=WGS84"
        .format(lon_a, lat_a, h_a))
    lon_b, lat_b, h_b = topo.transform(*corner)
    site_b = new_stage(W1_DIR / "siteB.usda")
    b = UsdGeom.Xform.Define(site_b, "/SiteB").GetPrim()
    site_b.SetDefaultPrim(b)
    bind(b, "/CRS/WGS84_Geographic3D", (lon_b, lat_b, h_b))
    box_mesh(site_b, "/SiteB/Mast", MAST, (0.2, 0.5, 0.9))
    site_b.GetRootLayer().Save()

    scene = new_stage(W1_DIR / "scene.usda")
    world = UsdGeom.Xform.Define(scene, "/World").GetPrim()
    scene.SetDefaultPrim(world)
    for name in ("SiteA", "SiteB"):
        scene.DefinePrim("/World/" + name).GetReferences().AddReference(
            "./{}.usda".format(name[0].lower() + name[1:]))
    scene.GetRootLayer().Save()
    print("wrote fixtures to {}".format(W1_DIR))


# ---------------------------------------------------------------------------
# check

def check(out_dir, tolerance):
    ok = True
    m = json.loads((out_dir / "manifest-global.json").read_text())
    meshes = {r["path"]: r for r in m["meshes"]}
    anchors = {a["path"]: a for a in m["anchors"]}
    ell = Ellipsoid.of(CRS("EPSG:4979"))

    def to_ecef(anchor_path, local):
        lon, lat, h = anchors[anchor_path]["origin"]
        return ell.to_ecef(lon, lat, h) + enu_basis(lon, lat) @ local

    building = meshes["/World/SiteA/Building"]
    mat = np.array(building["matrix"]).reshape(4, 4).T
    corner = mat @ np.array([BUILDING[0] / 2, BUILDING[1] / 2, 0.0, 1.0])
    d = np.linalg.norm(to_ecef("/World/SiteA", corner[:3])
                       - to_ecef("/World/SiteB", np.zeros(3)))
    ok &= report("mast stands on the building corner", d, tolerance)
    for r in m["meshes"]:
        if r["status"] == "ok":
            ok &= report("linearization error " + r["path"],
                         r["linearizationError"], tolerance)
    return ok


def report(label, value, tolerance):
    good = value <= tolerance
    print("  {} {:<50} {:.6f} m (tolerance {} m)".format(
        "ok " if good else "FAIL", label, value, tolerance))
    return good


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fixtures")
    s = sub.add_parser("export")
    s.add_argument("stage", nargs="?", default=str(W1_DIR / "scene.usda"))
    s.add_argument("--out", default=str(RECORDED_DIR))
    s = sub.add_parser("check")
    s.add_argument("--out", default=str(RECORDED_DIR))
    s.add_argument("--tolerance", type=float, default=0.001)
    args = p.parse_args()
    if args.cmd == "fixtures":
        fixtures()
    elif args.cmd == "export":
        export(Path(args.stage).resolve(), Path(args.out))
    else:
        sys.exit(0 if check(Path(args.out), args.tolerance) else 1)


if __name__ == "__main__":
    main()
