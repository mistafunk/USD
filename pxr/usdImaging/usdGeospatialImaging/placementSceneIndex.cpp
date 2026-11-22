//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdGeospatialImaging/placementSceneIndex.h"

#include "pxr/usdImaging/usdGeospatialImaging/anchorSchema.h"
#include "pxr/usdImaging/usdGeospatialImaging/bindingSchema.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSchema.h"
#include "pxr/usdImaging/usdGeospatialImaging/tokens.h"

#include "pxr/usd/usdGeom/tokens.h"

#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPrimView.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/vt/array.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

template <typename T>
T
_GetValue(const HdContainerDataSourceHandle &container,
          const TfToken &name, const T &fallback)
{
    if (const auto ds = HdTypedSampledDataSource<T>::Cast(
            container ? container->Get(name) : nullptr)) {
        return ds->GetTypedValue(0.0f);
    }
    return fallback;
}

template <typename T>
HdDataSourceBaseHandle
_Retained(const T &value)
{
    return HdRetainedTypedSampledDataSource<T>::New(value);
}

std::optional<GfMatrix4d>
_GetWorldMatrix(const HdContainerDataSourceHandle &primSource)
{
    if (const HdMatrixDataSourceHandle ds =
            HdXformSchema::GetFromParent(primSource).GetMatrix()) {
        return ds->GetTypedValue(0.0f);
    }
    return std::nullopt;
}

const TfToken &
_StatusToken(UsdGeospatialPlacement::Status status)
{
    switch (status) {
    case UsdGeospatialPlacement::Status::Ok:
        return UsdGeospatialImagingPlacementSchemaTokens->ok;
    case UsdGeospatialPlacement::Status::Unsupported:
        return UsdGeospatialImagingPlacementSchemaTokens->unsupported;
    default:
        return UsdGeospatialImagingPlacementSchemaTokens->error;
    }
}

// Display coordinates (metres east/north/up or output CRS units) to the
// stage's units and up axis, so a viewer's camera conventions still apply.
GfMatrix4d
_DisplayToStage(double metersPerUnit, const TfToken &upAxis)
{
    const double s = metersPerUnit > 0.0 ? 1.0 / metersPerUnit : 1.0;
    GfMatrix4d m(1.0);
    if (upAxis == UsdGeomTokens->y) {
        // (east, north, up) -> (x, y, z) = (east, up, -north)
        m.SetRow(0, GfVec4d(s, 0, 0, 0));
        m.SetRow(1, GfVec4d(0, 0, -s, 0));
        m.SetRow(2, GfVec4d(0, s, 0, 0));
    } else {
        m.SetScale(s);
    }
    return m;
}

// The exact point map of a mesh's points into the anchor's frame, and the
// largest distance between it and the matrix, in frame units.
struct _ExactPoints
{
    VtVec3fArray framePoints;
    double linearizationError = 0.0;
};

std::optional<_ExactPoints>
_ComputeExactPoints(const UsdGeospatialPlacement &placement,
                    const HdContainerDataSourceHandle &primSource,
                    const GfMatrix4d &toAnchor,
                    const GfMatrix4d &modelToFrame)
{
    const HdSampledDataSourceHandle pointsDs =
        HdPrimvarsSchema::GetFromParent(primSource)
            .GetPrimvar(HdTokens->points).GetPrimvarValue();
    if (!pointsDs) {
        return std::nullopt;
    }
    const VtValue value = pointsDs->GetValue(0.0f);
    if (!value.IsHolding<VtVec3fArray>()) {
        return std::nullopt;
    }
    const VtVec3fArray &points = value.UncheckedGet<VtVec3fArray>();
    std::vector<GfVec3d> model;
    model.reserve(points.size());
    for (const GfVec3f &p : points) {
        model.push_back(toAnchor.Transform(GfVec3d(p)));
    }
    std::vector<GfVec3d> exact;
    if (!placement.ComputeFramePoints(model, &exact)) {
        return std::nullopt;
    }
    _ExactPoints result;
    result.framePoints.resize(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        const GfVec3d linear = modelToFrame.Transform(GfVec3d(points[i]));
        result.linearizationError = std::max(
            result.linearizationError, (exact[i] - linear).GetLength());
        result.framePoints[i] = GfVec3f(exact[i]);
    }
    return result;
}

} // anonymous namespace

UsdGeospatialImagingPlacementSceneIndexRefPtr
UsdGeospatialImagingPlacementSceneIndex::New(
    const HdSceneIndexBaseRefPtr &inputSceneIndex,
    const HdContainerDataSourceHandle &inputArgs)
{
    return TfCreateRefPtr(
        new UsdGeospatialImagingPlacementSceneIndex(
            inputSceneIndex, inputArgs));
}

UsdGeospatialImagingPlacementSceneIndex::
UsdGeospatialImagingPlacementSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex,
    const HdContainerDataSourceHandle &inputArgs)
  : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
  , _query(
      _GetValue<std::string>(
          inputArgs, UsdGeospatialImagingPlacementSceneIndexTokens->outputCrs,
          std::string()),
      _GetValue<TfToken>(
          inputArgs, UsdGeospatialImagingPlacementSceneIndexTokens->engine,
          TfToken()).GetString())
  , _applyToXform(
      _GetValue<bool>(
          inputArgs,
          UsdGeospatialImagingPlacementSceneIndexTokens->applyToXform,
          false))
  , _measureLinearization(
      _GetValue<bool>(
          inputArgs,
          UsdGeospatialImagingPlacementSceneIndexTokens->measureLinearization,
          false))
  , _exactPoints(
      _GetValue<bool>(
          inputArgs,
          UsdGeospatialImagingPlacementSceneIndexTokens->exactPoints,
          false))
{
    if (const auto ds = HdVec3dDataSource::Cast(
            inputArgs ? inputArgs->Get(
                UsdGeospatialImagingPlacementSceneIndexTokens->displayOrigin)
                      : nullptr)) {
        _displayOrigin = ds->GetTypedValue(0.0f);
    }
    if (!_query.IsValid()) {
        TF_WARN("Geospatial placement is not possible: %s",
                _query.GetError().c_str());
    }
}

UsdGeospatialImagingPlacementSceneIndex::
~UsdGeospatialImagingPlacementSceneIndex() = default;

HdSceneIndexPrim
UsdGeospatialImagingPlacementSceneIndex::GetPrim(
    const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (!prim.dataSource) {
        return prim;
    }
    const HdPathDataSourceHandle anchorDs =
        UsdGeospatialImagingBindingSchema::GetFromParent(prim.dataSource)
            .GetAnchor();
    if (!anchorDs) {
        return prim;
    }
    const SdfPath anchorPath = anchorDs->GetTypedValue(0.0f);
    const _AnchorSharedPtr anchor = _GetAnchor(anchorPath);
    const UsdGeospatialPlacement &placement = anchor->placement;

    UsdGeospatialImagingPlacementSchema::Builder builder;
    if (!placement.GetMissingGrids().empty()) {
        const std::vector<std::string>& grids = placement.GetMissingGrids();
        builder.SetMissingGrids(
            HdRetainedTypedSampledDataSource<VtStringArray>::New(
                VtStringArray(grids.begin(), grids.end())));
    }
    builder
        .SetStatus(HdRetainedTypedSampledDataSource<TfToken>::New(
            _StatusToken(placement.GetStatus())))
        .SetAnchor(HdRetainedTypedSampledDataSource<SdfPath>::New(
            anchorPath))
        .SetEngine(HdRetainedTypedSampledDataSource<std::string>::New(
            placement.GetEngine()));

    TfTokenVector names;
    std::vector<HdDataSourceBaseHandle> sources;

    if (!placement.IsOk()) {
        builder.SetReason(HdRetainedTypedSampledDataSource<std::string>::New(
            placement.GetReason()));
        names.push_back(HdVisibilitySchema::GetSchemaToken());
        sources.push_back(HdVisibilitySchema::Builder()
            .SetVisibility(HdRetainedTypedSampledDataSource<bool>::New(false))
            .Build());
    } else {
        builder
            .SetOperation(HdRetainedTypedSampledDataSource<std::string>::New(
                placement.GetOperation()))
            .SetSourceCrs(HdRetainedTypedSampledDataSource<std::string>::New(
                placement.GetSourceCrs()))
            .SetOrigin(HdRetainedTypedSampledDataSource<GfVec3d>::New(
                placement.GetOrigin()))
            .SetFrame(HdRetainedTypedSampledDataSource<TfToken>::New(
                placement.IsLocalFrame()
                    ? UsdGeospatialImagingPlacementSchemaTokens->enu
                    : UsdGeospatialImagingPlacementSchemaTokens->crs));
        if (!placement.GetSkippedOperations().empty()) {
            const std::vector<std::string>& skipped =
                placement.GetSkippedOperations();
            builder.SetSkippedOperations(
                HdRetainedTypedSampledDataSource<VtStringArray>::New(
                    VtStringArray(skipped.begin(), skipped.end())));
        }
        if (placement.GetAccuracy() >= 0.0) {
            builder.SetAccuracy(HdRetainedTypedSampledDataSource<double>::New(
                placement.GetAccuracy()));
        }

        if (const std::optional<GfMatrix4d> world =
                _GetWorldMatrix(prim.dataSource)) {
            const GfMatrix4d toAnchor = *world * anchor->inverseWorld;
            const GfMatrix4d modelToFrame =
                toAnchor * placement.GetModelToFrame();
            builder.SetModelToFrame(
                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                    modelToFrame));

            if ((_measureLinearization || _exactPoints) &&
                prim.primType == HdPrimTypeTokens->mesh) {
                if (const std::optional<_ExactPoints> exact =
                        _ComputeExactPoints(placement, prim.dataSource,
                                            toAnchor, modelToFrame)) {
                    builder.SetLinearizationError(
                        HdRetainedTypedSampledDataSource<double>::New(
                            exact->linearizationError));
                    if (_exactPoints) {
                        builder.SetFramePoints(
                            HdRetainedTypedSampledDataSource<VtVec3fArray>::New(
                                exact->framePoints));
                    }
                }
            }

            if (_applyToXform) {
                const GfMatrix4d display =
                    modelToFrame * anchor->frameToDisplay;
                names.push_back(HdXformSchema::GetSchemaToken());
                sources.push_back(HdXformSchema::Builder()
                    .SetMatrix(
                        HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                            display))
                    .SetResetXformStack(
                        HdRetainedTypedSampledDataSource<bool>::New(true))
                    .Build());
            }
        }
    }

    names.push_back(UsdGeospatialImagingPlacementSchema::GetSchemaToken());
    sources.push_back(builder.Build());

    prim.dataSource = HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            names.size(), names.data(), sources.data()),
        prim.dataSource);
    return prim;
}

SdfPathVector
UsdGeospatialImagingPlacementSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

UsdGeospatialImagingPlacementSceneIndex::_AnchorSharedPtr
UsdGeospatialImagingPlacementSceneIndex::_GetAnchor(
    const SdfPath &anchorPath) const
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _anchors.find(anchorPath);
        if (it != _anchors.end()) {
            return it->second;
        }
    }
    // Computed outside the lock; two threads may both compute an anchor,
    // with the same result.
    _AnchorSharedPtr anchor = _ComputeAnchor(anchorPath);
    std::lock_guard<std::mutex> lock(_mutex);
    return _anchors.emplace(anchorPath, std::move(anchor)).first->second;
}

UsdGeospatialImagingPlacementSceneIndex::_AnchorSharedPtr
UsdGeospatialImagingPlacementSceneIndex::_ComputeAnchor(
    const SdfPath &anchorPath) const
{

    auto result = std::make_shared<_Anchor>();
    const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(anchorPath);
    const HdContainerDataSourceHandle anchorSource =
        UsdGeospatialImagingAnchorSchema::GetFromParent(prim.dataSource)
            .GetContainer();

    UsdGeospatialPlacementInputs inputs;
    inputs.anchorPath = anchorPath;
    inputs.error = anchorSource
        ? _GetValue<std::string>(
              anchorSource, UsdGeospatialImagingAnchorSchemaTokens->error,
              std::string())
        : TfStringPrintf("<%s> has no geospatial anchor data",
                         anchorPath.GetText());
    inputs.wkt = _GetValue<std::string>(
        anchorSource, UsdGeospatialImagingAnchorSchemaTokens->crsWkt,
        std::string());
    inputs.position = _GetValue<GfVec3d>(
        anchorSource, UsdGeospatialImagingAnchorSchemaTokens->position,
        GfVec3d(0.0));
    inputs.attitude = _GetValue<GfMatrix4d>(
        anchorSource, UsdGeospatialImagingAnchorSchemaTokens->attitude,
        GfMatrix4d(1.0)).ExtractRotationQuat();
    inputs.metersPerUnit = _GetValue<double>(
        anchorSource, UsdGeospatialImagingAnchorSchemaTokens->metersPerUnit,
        1.0);
    inputs.upAxis = _GetValue<TfToken>(
        anchorSource, UsdGeospatialImagingAnchorSchemaTokens->upAxis,
        UsdGeomTokens->z);
    inputs.hasOwnTransform = _GetValue<bool>(
        anchorSource, UsdGeospatialImagingAnchorSchemaTokens->hasOwnTransform,
        false);

    result->placement = _query.Resolve(inputs);
    if (!result->placement.IsOk()) {
        return result;
    }

    const std::optional<GfMatrix4d> world = _GetWorldMatrix(prim.dataSource);
    result->inverseWorld = world ? world->GetInverse() : GfMatrix4d(1.0);
    result->frameToDisplay =
        _ComputeFrameToDisplay(result->placement) *
        _DisplayToStage(inputs.metersPerUnit, inputs.upAxis);
    return result;
}

GfMatrix4d
UsdGeospatialImagingPlacementSceneIndex::_ComputeFrameToDisplay(
    const UsdGeospatialPlacement &placement) const
{
    GfVec3d displayOrigin;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_displayOrigin) {
            _displayOrigin = placement.GetOrigin();
        }
        displayOrigin = *_displayOrigin;
    }

    if (!placement.IsLocalFrame()) {
        // Output CRS units relative to the display origin.
        GfMatrix4d m(1.0);
        m.SetTranslate(placement.GetOrigin() - displayOrigin);
        return m;
    }
    // East/north/up at the anchor, through geocentric coordinates of the
    // output datum, into east/north/up at the display origin.
    const UsdGeospatialCrsInfo &output = _query.GetOutputInfo();
    return UsdGeospatialComputeEnuToEcef(output, placement.GetOrigin()) *
        UsdGeospatialComputeEnuToEcef(output, displayOrigin).GetInverse();
}

void
UsdGeospatialImagingPlacementSceneIndex::_InvalidateAnchors(
    const SdfPath &root, bool descendants)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!descendants) {
        _anchors.erase(root);
        return;
    }
    for (auto it = _anchors.begin(); it != _anchors.end(); ) {
        if (it->first.HasPrefix(root)) {
            it = _anchors.erase(it);
        } else {
            ++it;
        }
    }
}

void
UsdGeospatialImagingPlacementSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    for (const HdSceneIndexObserver::AddedPrimEntry &entry : entries) {
        _InvalidateAnchors(entry.primPath, /* descendants = */ false);
    }
    _SendPrimsAdded(entries);
}

void
UsdGeospatialImagingPlacementSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    for (const HdSceneIndexObserver::RemovedPrimEntry &entry : entries) {
        _InvalidateAnchors(entry.primPath, /* descendants = */ true);
    }
    _SendPrimsRemoved(entries);
}

void
UsdGeospatialImagingPlacementSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    static const HdDataSourceLocatorSet placementInputs{
        UsdGeospatialImagingAnchorSchema::GetDefaultLocator(),
        HdXformSchema::GetDefaultLocator() };
    static const HdDataSourceLocatorSet primInputs{
        UsdGeospatialImagingBindingSchema::GetDefaultLocator(),
        HdXformSchema::GetDefaultLocator() };
    static const HdDataSourceLocatorSet placementOutputs{
        UsdGeospatialImagingPlacementSchema::GetDefaultLocator(),
        HdXformSchema::GetDefaultLocator(),
        HdVisibilitySchema::GetDefaultLocator() };

    HdSceneIndexObserver::DirtiedPrimEntries dirtied(entries);
    SdfPathVector changedAnchors;
    for (const HdSceneIndexObserver::DirtiedPrimEntry &entry : entries) {
        if (entry.dirtyLocators.Intersects(placementInputs)) {
            bool isAnchor = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                isAnchor = _anchors.count(entry.primPath) > 0;
            }
            if (isAnchor) {
                _InvalidateAnchors(entry.primPath, /* descendants = */ false);
                changedAnchors.push_back(entry.primPath);
                continue;
            }
        }
        if (entry.dirtyLocators.Intersects(primInputs)) {
            dirtied.emplace_back(entry.primPath, placementOutputs);
        }
    }

    // Everything placed through a changed anchor changes with it.
    for (const SdfPath &anchorPath : changedAnchors) {
        for (const SdfPath &path :
                 HdSceneIndexPrimView(_GetInputSceneIndex(), anchorPath)) {
            dirtied.emplace_back(path, placementOutputs);
        }
    }

    _SendPrimsDirtied(dirtied);
}

PXR_NAMESPACE_CLOSE_SCOPE
