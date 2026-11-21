//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_BINDING_API_ADAPTER_H
#define PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_BINDING_API_ADAPTER_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/api.h"

#include "pxr/usdImaging/usdImaging/apiSchemaAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdGeospatialImagingBindingAPIAdapter
///
/// Publishes a directly bound prim (GeospatialCRSBindingAPI) to Hydra: the
/// geospatialBinding container, which the flattening scene index passes
/// down to descendants, and the anchor's placement inputs as the
/// geospatialAnchor container.
///
class UsdGeospatialImagingBindingAPIAdapter : public UsdImagingAPISchemaAdapter
{
public:
    using BaseAdapter = UsdImagingAPISchemaAdapter;

    USDGEOSPATIALIMAGING_API
    UsdGeospatialImagingBindingAPIAdapter();

    USDGEOSPATIALIMAGING_API
    ~UsdGeospatialImagingBindingAPIAdapter() override;

    USDGEOSPATIALIMAGING_API
    HdContainerDataSourceHandle GetImagingSubprimData(
            UsdPrim const& prim,
            TfToken const& subprim,
            TfToken const& appliedInstanceName,
            const UsdImagingDataSourceStageGlobals &stageGlobals) override;

    USDGEOSPATIALIMAGING_API
    HdDataSourceLocatorSet InvalidateImagingSubprim(
            UsdPrim const& prim,
            TfToken const& subprim,
            TfToken const& appliedInstanceName,
            TfTokenVector const& properties,
            UsdImagingPropertyInvalidationType invalidationType) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
