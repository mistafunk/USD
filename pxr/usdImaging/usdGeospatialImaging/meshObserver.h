//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_MESH_OBSERVER_H
#define PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_MESH_OBSERVER_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/api.h"

#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/token.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdGeospatialImagingMeshObserver
///
/// Keeps what a minimal mesh renderer needs from a scene index that
/// includes UsdGeospatialImagingPlacementSceneIndex: per mesh, flat-shaded
/// triangles and the placement matrix into its anchor's frame; per model
/// anchor, the placement result. Replaces a render delegate for clients that
/// draw placed meshes themselves (the geospatial web viewer).
///
/// Notices only mark prims dirty; Sync() reads them, so a client decides
/// when to pay for the update.
///
class UsdGeospatialImagingMeshObserver : public HdSceneIndexObserver
{
public:
    struct Anchor
    {
        SdfPath path;
        /// ok, unsupported or error (UsdGeospatialImagingPlacementSchema).
        TfToken status;
        std::string reason;
        std::string engine;
        std::string operation;
        /// Metres; empty if unknown.
        std::optional<double> accuracy;
        /// More accurate operations skipped for missing data (grids).
        std::vector<std::string> skippedOperations;
        std::string sourceCrs;
        GfVec3d position{0.0};
        GfVec3d orientation{0.0};
        GfVec3d origin{0.0};
        /// enu or crs.
        TfToken frame;
        GfMatrix4d modelToFrame{1.0};
    };

    struct Mesh
    {
        SdfPath path;
        /// Empty if the mesh has no CRS binding.
        SdfPath anchor;
        /// The anchor's status, or "unplaced" without a binding.
        TfToken status;
        std::string reason;
        GfVec3f color{0.8f};
        GfMatrix4d modelToFrame{1.0};
        std::optional<double> linearizationError;
        /// Flat-shaded, de-indexed triangles in mesh-local stage units:
        /// xyz triples and indices 0..n-1.
        std::vector<float> points;
        std::vector<float> normals;
        std::vector<uint32_t> indices;
        /// With exact points (placement scene index exactPoints): the same
        /// triangles' corners mapped exactly into the anchor's frame,
        /// relative to its origin; else empty.
        std::vector<float> framePoints;
    };

    /// Observes \p sceneIndex and reads all prims it has.
    USDGEOSPATIALIMAGING_API
    explicit UsdGeospatialImagingMeshObserver(
        const HdSceneIndexBaseRefPtr &sceneIndex);

    USDGEOSPATIALIMAGING_API
    ~UsdGeospatialImagingMeshObserver() override;

    /// Reads the prims changed since the last call. Returns the meshes and
    /// anchors that changed or were removed.
    USDGEOSPATIALIMAGING_API
    SdfPathVector Sync();

    const std::map<SdfPath, Mesh> &GetMeshes() const { return _meshes; }
    const std::map<SdfPath, Anchor> &GetAnchors() const { return _anchors; }

    // HdSceneIndexObserver
    USDGEOSPATIALIMAGING_API
    void PrimsAdded(const HdSceneIndexBase &sender,
                    const AddedPrimEntries &entries) override;
    USDGEOSPATIALIMAGING_API
    void PrimsRemoved(const HdSceneIndexBase &sender,
                      const RemovedPrimEntries &entries) override;
    USDGEOSPATIALIMAGING_API
    void PrimsDirtied(const HdSceneIndexBase &sender,
                      const DirtiedPrimEntries &entries) override;
    USDGEOSPATIALIMAGING_API
    void PrimsRenamed(const HdSceneIndexBase &sender,
                      const RenamedPrimEntries &entries) override;

private:
    void _Read(const SdfPath &path, SdfPathVector *changed);
    void _Erase(const SdfPath &root, SdfPathVector *changed);

    HdSceneIndexBaseRefPtr _sceneIndex;
    std::map<SdfPath, Mesh> _meshes;
    std::map<SdfPath, Anchor> _anchors;
    std::set<SdfPath> _dirty;
    std::set<SdfPath> _removed;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
