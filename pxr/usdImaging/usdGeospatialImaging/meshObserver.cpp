//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdGeospatialImaging/meshObserver.h"

#include "pxr/usdImaging/usdGeospatialImaging/anchorSchema.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSchema.h"

#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/sceneIndexPrimView.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/vt/array.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (unplaced)
    (displayColor)
);

namespace {

template <typename T, typename Handle>
std::optional<T>
_Get(const Handle &ds)
{
    if (ds) {
        return ds->GetTypedValue(0.0f);
    }
    return std::nullopt;
}

template <typename T, typename Handle>
T
_Get(const Handle &ds, const T &fallback)
{
    return ds ? T(ds->GetTypedValue(0.0f)) : fallback;
}

VtValue
_GetPrimvar(const HdContainerDataSourceHandle &primSource,
            const TfToken &name)
{
    const HdSampledDataSourceHandle ds =
        HdPrimvarsSchema::GetFromParent(primSource)
            .GetPrimvar(name).GetPrimvarValue();
    return ds ? ds->GetValue(0.0f) : VtValue();
}

// Flat-shaded triangles: a fan from each face's first vertex, reversed for
// left-handed orientation, with one face normal per corner.
void
_Triangulate(const VtVec3fArray &points,
             const VtVec3fArray &framePoints,
             const VtIntArray &counts,
             const VtIntArray &indices,
             bool leftHanded,
             UsdGeospatialImagingMeshObserver::Mesh *mesh)
{
    mesh->points.clear();
    mesh->normals.clear();
    mesh->indices.clear();
    mesh->framePoints.clear();
    const bool exact = framePoints.size() == points.size();

    auto point = [&points](int i) {
        return (i >= 0 && size_t(i) < points.size())
            ? GfVec3d(points[i]) : GfVec3d(0.0);
    };

    size_t start = 0;
    std::vector<int> face;
    for (const int n : counts) {
        if (n < 0 || start + n > indices.size()) {
            break;
        }
        face.assign(indices.begin() + start, indices.begin() + start + n);
        start += n;
        if (leftHanded) {
            std::reverse(face.begin(), face.end());
        }
        for (int k = 1; k + 1 < n; ++k) {
            const int ids[3] = { face[0], face[k], face[k + 1] };
            const GfVec3d corners[3] = {
                point(ids[0]), point(ids[1]), point(ids[2]) };
            GfVec3d normal =
                GfCross(corners[1] - corners[0], corners[2] - corners[0]);
            const double length = normal.GetLength();
            if (length > 0.0) {
                normal /= length;
            }
            for (int c = 0; c < 3; ++c) {
                mesh->indices.push_back(
                    static_cast<uint32_t>(mesh->points.size() / 3));
                for (int j = 0; j < 3; ++j) {
                    mesh->points.push_back(static_cast<float>(corners[c][j]));
                    mesh->normals.push_back(static_cast<float>(normal[j]));
                    if (exact) {
                        const int id = ids[c];
                        mesh->framePoints.push_back(
                            id >= 0 && size_t(id) < framePoints.size()
                                ? framePoints[id][j] : 0.0f);
                    }
                }
            }
        }
    }
}

} // anonymous namespace

UsdGeospatialImagingMeshObserver::UsdGeospatialImagingMeshObserver(
    const HdSceneIndexBaseRefPtr &sceneIndex)
  : _sceneIndex(sceneIndex)
{
    for (const SdfPath &path : HdSceneIndexPrimView(_sceneIndex)) {
        _dirty.insert(path);
    }
    _sceneIndex->AddObserver(HdSceneIndexObserverPtr(this));
}

UsdGeospatialImagingMeshObserver::~UsdGeospatialImagingMeshObserver()
{
    _sceneIndex->RemoveObserver(HdSceneIndexObserverPtr(this));
}

SdfPathVector
UsdGeospatialImagingMeshObserver::Sync()
{
    SdfPathVector changed;
    for (const SdfPath &path : _removed) {
        _Erase(path, &changed);
    }
    _removed.clear();
    for (const SdfPath &path : _dirty) {
        _Read(path, &changed);
    }
    _dirty.clear();
    return changed;
}

void
UsdGeospatialImagingMeshObserver::_Erase(const SdfPath &root,
                                         SdfPathVector *changed)
{
    for (auto it = _meshes.lower_bound(root);
         it != _meshes.end() && it->first.HasPrefix(root); ) {
        changed->push_back(it->first);
        it = _meshes.erase(it);
    }
    for (auto it = _anchors.lower_bound(root);
         it != _anchors.end() && it->first.HasPrefix(root); ) {
        changed->push_back(it->first);
        it = _anchors.erase(it);
    }
}

void
UsdGeospatialImagingMeshObserver::_Read(const SdfPath &path,
                                        SdfPathVector *changed)
{

    const HdSceneIndexPrim prim = _sceneIndex->GetPrim(path);
    const UsdGeospatialImagingPlacementSchema placement =
        UsdGeospatialImagingPlacementSchema::GetFromParent(prim.dataSource);

    // A model anchor: directly bound, so it carries the placement inputs.
    const UsdGeospatialImagingAnchorSchema inputs =
        UsdGeospatialImagingAnchorSchema::GetFromParent(prim.dataSource);
    if (inputs && placement) {
        Anchor &anchor = _anchors[path];
        anchor = Anchor();
        anchor.path = path;
        anchor.status = _Get(placement.GetStatus(),
                             UsdGeospatialImagingPlacementSchemaTokens->error);
        anchor.reason = _Get(placement.GetReason(), std::string());
        anchor.engine = _Get(placement.GetEngine(), std::string());
        anchor.operation = _Get(placement.GetOperation(), std::string());
        anchor.accuracy = _Get<double>(placement.GetAccuracy());
        const VtStringArray skipped =
            _Get(placement.GetSkippedOperations(), VtStringArray());
        anchor.skippedOperations.assign(skipped.begin(), skipped.end());
        anchor.sourceCrs = _Get(placement.GetSourceCrs(), std::string());
        anchor.position = _Get(inputs.GetPosition(), GfVec3d(0.0));
        anchor.orientation = _Get(inputs.GetOrientation(), GfVec3d(0.0));
        anchor.origin = _Get(placement.GetOrigin(), GfVec3d(0.0));
        anchor.frame = _Get(placement.GetFrame(), TfToken());
        anchor.modelToFrame =
            _Get(placement.GetModelToFrame(), GfMatrix4d(1.0));
        changed->push_back(path);
    } else if (_anchors.erase(path)) {
        changed->push_back(path);
    }

    if (prim.primType != HdPrimTypeTokens->mesh) {
        if (_meshes.erase(path)) {
            changed->push_back(path);
        }
        return;
    }

    Mesh &mesh = _meshes[path];
    mesh = Mesh();
    mesh.path = path;
    if (placement) {
        mesh.anchor = _Get(placement.GetAnchor(), SdfPath());
        mesh.status = _Get(placement.GetStatus(),
                           UsdGeospatialImagingPlacementSchemaTokens->error);
        mesh.reason = _Get(placement.GetReason(), std::string());
        mesh.modelToFrame =
            _Get(placement.GetModelToFrame(), GfMatrix4d(1.0));
        mesh.linearizationError =
            _Get<double>(placement.GetLinearizationError());
    } else {
        mesh.status = _tokens->unplaced;
    }

    const VtValue color = _GetPrimvar(prim.dataSource, _tokens->displayColor);
    if (color.IsHolding<VtVec3fArray>() &&
        !color.UncheckedGet<VtVec3fArray>().empty()) {
        mesh.color = color.UncheckedGet<VtVec3fArray>()[0];
    } else if (color.IsHolding<GfVec3f>()) {
        mesh.color = color.UncheckedGet<GfVec3f>();
    }

    const VtValue points = _GetPrimvar(prim.dataSource, HdTokens->points);
    const HdMeshTopologySchema topology =
        HdMeshSchema::GetFromParent(prim.dataSource).GetTopology();
    if (points.IsHolding<VtVec3fArray>() && topology) {
        _Triangulate(
            points.UncheckedGet<VtVec3fArray>(),
            placement ? _Get(placement.GetFramePoints(), VtVec3fArray())
                      : VtVec3fArray(),
            _Get(topology.GetFaceVertexCounts(), VtIntArray()),
            _Get(topology.GetFaceVertexIndices(), VtIntArray()),
            _Get(topology.GetOrientation(), TfToken()) ==
                HdTokens->leftHanded,
            &mesh);
    }
    changed->push_back(path);
}

void
UsdGeospatialImagingMeshObserver::PrimsAdded(
    const HdSceneIndexBase &sender,
    const AddedPrimEntries &entries)
{
    for (const AddedPrimEntry &entry : entries) {
        _dirty.insert(entry.primPath);
    }
}

void
UsdGeospatialImagingMeshObserver::PrimsRemoved(
    const HdSceneIndexBase &sender,
    const RemovedPrimEntries &entries)
{
    for (const RemovedPrimEntry &entry : entries) {
        _removed.insert(entry.primPath);
        for (auto it = _dirty.lower_bound(entry.primPath);
             it != _dirty.end() && it->HasPrefix(entry.primPath); ) {
            it = _dirty.erase(it);
        }
    }
}

void
UsdGeospatialImagingMeshObserver::PrimsDirtied(
    const HdSceneIndexBase &sender,
    const DirtiedPrimEntries &entries)
{
    for (const DirtiedPrimEntry &entry : entries) {
        _dirty.insert(entry.primPath);
    }
}

void
UsdGeospatialImagingMeshObserver::PrimsRenamed(
    const HdSceneIndexBase &sender,
    const RenamedPrimEntries &entries)
{
    ConvertPrimsRenamedToRemovedAndAdded(sender, entries, this);
}

PXR_NAMESPACE_CLOSE_SCOPE
