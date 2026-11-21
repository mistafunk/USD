//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_PLACEMENT_SCENE_INDEX_H
#define PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_PLACEMENT_SCENE_INDEX_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/api.h"

#include "pxr/usd/usdGeospatial/placement.h"

#include "pxr/imaging/hd/filteringSceneIndex.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(UsdGeospatialImagingPlacementSceneIndex);

/// \class UsdGeospatialImagingPlacementSceneIndex
///
/// Places geo-referenced models in one output CRS. It expects the
/// geospatialBinding container to be flattened (each prim knows its model
/// anchor) and transforms to be flattened (world matrices), so it runs after
/// usdImaging's flattening scene index.
///
/// For every prim with a CRS binding it adds a geospatialPlacement container
/// (UsdGeospatialImagingPlacementSchema): the anchor's status, the engine's
/// operation and accuracy, the anchor in the output CRS, and the prim's
/// matrix into the anchor's frame. Prims whose anchor cannot be placed are
/// made invisible; the placement container says why.
///
/// Input arguments (UsdGeospatialImagingPlacementSceneIndexTokens):
/// - outputCrs (string): the output CRS, as the engine accepts it (e.g.
///   "EPSG:4979" or WKT2); it must be 3D (see UsdGeospatialPlacementQuery).
/// - engine (token): engine name; empty for the active engine.
/// - applyToXform (bool): also rewrite each placed prim's transform into a
///   display frame shared by all anchors, so a generic viewer such as usdview
///   shows the placed scene: metres east/north/up at displayOrigin for a
///   geographic output, output CRS units relative to displayOrigin
///   otherwise, both mapped to the stage's metersPerUnit and upAxis.
/// - displayOrigin (GfVec3d): in the output CRS; defaults to the origin of
///   the first anchor placed.
/// - measureLinearization (bool): for meshes, compare modelToFrame with the
///   exact point map at every point (linearizationError).
/// - exactPoints (bool): for meshes, also publish the exact point map of
///   every point (framePoints), the plan's measurement mode.
///
class UsdGeospatialImagingPlacementSceneIndex
    : public HdSingleInputFilteringSceneIndexBase
{
public:
    USDGEOSPATIALIMAGING_API
    static UsdGeospatialImagingPlacementSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex,
        const HdContainerDataSourceHandle &inputArgs);

    USDGEOSPATIALIMAGING_API
    ~UsdGeospatialImagingPlacementSceneIndex() override;

    USDGEOSPATIALIMAGING_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    USDGEOSPATIALIMAGING_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

    /// The query used to place anchors; invalid (with an error) if the
    /// output CRS or the engine cannot be used.
    const UsdGeospatialPlacementQuery &GetQuery() const { return _query; }

protected:
    UsdGeospatialImagingPlacementSceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex,
        const HdContainerDataSourceHandle &inputArgs);

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    // A placed (or failed) model anchor.
    struct _Anchor
    {
        UsdGeospatialPlacement placement;
        // Inverse of the anchor's world matrix, to express descendants
        // relative to the anchor.
        GfMatrix4d inverseWorld;
        // Frame coordinates to display coordinates (applyToXform).
        GfMatrix4d frameToDisplay;
    };
    using _AnchorSharedPtr = std::shared_ptr<const _Anchor>;

    _AnchorSharedPtr _GetAnchor(const SdfPath &anchorPath) const;
    _AnchorSharedPtr _ComputeAnchor(const SdfPath &anchorPath) const;
    GfMatrix4d _ComputeFrameToDisplay(
        const UsdGeospatialPlacement &placement) const;
    void _InvalidateAnchors(const SdfPath &root, bool descendants);

    UsdGeospatialPlacementQuery _query;
    bool _applyToXform = false;
    bool _measureLinearization = false;
    bool _exactPoints = false;

    mutable std::mutex _mutex;
    mutable std::unordered_map<SdfPath, _AnchorSharedPtr, SdfPath::Hash>
        _anchors;
    mutable std::optional<GfVec3d> _displayOrigin;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
