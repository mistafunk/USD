//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdGeospatialImaging/bindingAPIAdapter.h"

#include "pxr/usdImaging/usdGeospatialImaging/anchorSchema.h"
#include "pxr/usdImaging/usdGeospatialImaging/bindingSchema.h"

#include "pxr/usdImaging/usdImaging/dataSourceStageGlobals.h"

#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/usd/usdGeospatial/tokens.h"
#include "pxr/usd/usdGeom/xformable.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/tf/stringUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// The placement inputs of an anchor at the stage globals' current time.
// Every field is computed on request through
// UsdGeospatialComputePlacementInputs, so Hydra sees exactly what
// UsdGeospatialPlacementQuery::Resolve(UsdPrim) would read.
class _AnchorDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_AnchorDataSource);

    TfTokenVector GetNames() override
    {
        static const TfTokenVector names = {
            UsdGeospatialImagingAnchorSchemaTokens->error,
            UsdGeospatialImagingAnchorSchemaTokens->crsWkt,
            UsdGeospatialImagingAnchorSchemaTokens->position,
            UsdGeospatialImagingAnchorSchemaTokens->orientation,
            UsdGeospatialImagingAnchorSchemaTokens->attitude,
            UsdGeospatialImagingAnchorSchemaTokens->metersPerUnit,
            UsdGeospatialImagingAnchorSchemaTokens->upAxis,
            UsdGeospatialImagingAnchorSchemaTokens->hasOwnTransform,
        };
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        _FlagIfTimeVarying();
        const UsdTimeCode time = _stageGlobals.GetTime();

        if (name == UsdGeospatialImagingAnchorSchemaTokens->orientation) {
            const UsdAttribute attr = _prim.GetAttribute(
                UsdGeospatialTokens->crsOrientation);
            GfVec3d hpr(0.0);
            if (attr && attr.HasAuthoredValue()) {
                attr.Get(&hpr, time);
            }
            return HdRetainedTypedSampledDataSource<GfVec3d>::New(hpr);
        }

        const UsdGeospatialPlacementInputs inputs =
            UsdGeospatialComputePlacementInputs(_prim, time);

        if (name == UsdGeospatialImagingAnchorSchemaTokens->error) {
            return HdRetainedTypedSampledDataSource<std::string>::New(
                inputs.error);
        }
        if (name == UsdGeospatialImagingAnchorSchemaTokens->crsWkt) {
            return HdRetainedTypedSampledDataSource<std::string>::New(
                inputs.wkt);
        }
        if (name == UsdGeospatialImagingAnchorSchemaTokens->position) {
            return HdRetainedTypedSampledDataSource<GfVec3d>::New(
                inputs.position);
        }
        if (name == UsdGeospatialImagingAnchorSchemaTokens->attitude) {
            GfMatrix4d m(1.0);
            m.SetRotate(inputs.attitude);
            return HdRetainedTypedSampledDataSource<GfMatrix4d>::New(m);
        }
        if (name == UsdGeospatialImagingAnchorSchemaTokens->metersPerUnit) {
            return HdRetainedTypedSampledDataSource<double>::New(
                inputs.metersPerUnit);
        }
        if (name == UsdGeospatialImagingAnchorSchemaTokens->upAxis) {
            return HdRetainedTypedSampledDataSource<TfToken>::New(
                inputs.upAxis);
        }
        if (name == UsdGeospatialImagingAnchorSchemaTokens->hasOwnTransform) {
            return HdRetainedTypedSampledDataSource<bool>::New(
                inputs.hasOwnTransform);
        }
        return nullptr;
    }

private:
    _AnchorDataSource(const SdfPath &sceneIndexPath,
                      const UsdPrim &prim,
                      const UsdImagingDataSourceStageGlobals &stageGlobals)
      : _sceneIndexPath(sceneIndexPath)
      , _prim(prim)
      , _stageGlobals(stageGlobals)
    {
    }

    // Placement depends on the time through crs:position, crs:orientation
    // and the anchor's own transform. Flagged on every access, like
    // usdImaging's attribute data sources, since this container can outlive
    // an edit that makes one of them time-varying.
    void _FlagIfTimeVarying() const
    {
        const UsdAttribute position =
            _prim.GetAttribute(UsdGeospatialTokens->crsPosition);
        const UsdAttribute orientation =
            _prim.GetAttribute(UsdGeospatialTokens->crsOrientation);
        if ((position && position.ValueMightBeTimeVarying()) ||
            (orientation && orientation.ValueMightBeTimeVarying()) ||
            UsdGeomXformable(_prim).TransformMightBeTimeVarying()) {
            _stageGlobals.FlagAsTimeVarying(
                _sceneIndexPath,
                UsdGeospatialImagingAnchorSchema::GetDefaultLocator());
        }
    }

    const SdfPath _sceneIndexPath;
    const UsdPrim _prim;
    const UsdImagingDataSourceStageGlobals &_stageGlobals;
};

bool
_AffectsPlacement(const TfToken &property)
{
    return property == UsdGeospatialTokens->crsPosition ||
           property == UsdGeospatialTokens->crsOrientation ||
           property == UsdGeospatialTokens->crsWkt ||
           property == UsdGeomTokens->xformOpOrder ||
           TfStringStartsWith(property.GetString(), "xformOp:");
}

} // anonymous namespace

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdGeospatialImagingBindingAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingAPISchemaAdapterFactory<Adapter> >();
}

UsdGeospatialImagingBindingAPIAdapter::
UsdGeospatialImagingBindingAPIAdapter() = default;

UsdGeospatialImagingBindingAPIAdapter::
~UsdGeospatialImagingBindingAPIAdapter() = default;

HdContainerDataSourceHandle
UsdGeospatialImagingBindingAPIAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty()) {
        return nullptr;
    }

    return HdRetainedContainerDataSource::New(
        UsdGeospatialImagingBindingSchema::GetSchemaToken(),
        UsdGeospatialImagingBindingSchema::Builder()
            .SetAnchor(HdRetainedTypedSampledDataSource<SdfPath>::New(
                prim.GetPath()))
            .Build(),
        UsdGeospatialImagingAnchorSchema::GetSchemaToken(),
        _AnchorDataSource::New(prim.GetPath(), prim, stageGlobals));
}

HdDataSourceLocatorSet
UsdGeospatialImagingBindingAPIAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty()) {
        return {};
    }

    for (const TfToken &property : properties) {
        if (_AffectsPlacement(property)) {
            return { UsdGeospatialImagingAnchorSchema::GetDefaultLocator() };
        }
    }
    return {};
}

PXR_NAMESPACE_CLOSE_SCOPE
