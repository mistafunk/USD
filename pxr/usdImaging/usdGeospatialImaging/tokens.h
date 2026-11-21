//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_TOKENS_H
#define PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_TOKENS_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/api.h"

#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Input arguments of UsdGeospatialImagingPlacementSceneIndex.
#define USD_GEOSPATIAL_IMAGING_PLACEMENT_SCENE_INDEX_TOKENS \
    (outputCrs)                                             \
    (engine)                                                \
    (applyToXform)                                          \
    (displayOrigin)                                         \
    (measureLinearization)

TF_DECLARE_PUBLIC_TOKENS(
    UsdGeospatialImagingPlacementSceneIndexTokens, USDGEOSPATIALIMAGING_API,
    USD_GEOSPATIAL_IMAGING_PLACEMENT_SCENE_INDEX_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
