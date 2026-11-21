//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_SCENE_INDEX_PLUGIN_H
#define PXR_USD_IMAGING_USD_GEOSPATIAL_IMAGING_SCENE_INDEX_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/api.h"

#include "pxr/usdImaging/usdImaging/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdGeospatialImagingSceneIndexPlugin
///
/// Has usdImaging's flattening scene index pass geospatialBinding down to
/// descendants. If USDGEOSPATIALIMAGING_OUTPUT_CRS is set, it also appends a
/// UsdGeospatialImagingPlacementSceneIndex that rewrites transforms into a
/// display frame, so any Hydra client (e.g. usdview) shows geo-referenced
/// models placed in that CRS; USDGEOSPATIALIMAGING_DISPLAY_ORIGIN
/// ("x, y, z" in the output CRS) fixes the display origin.
///
/// Clients that need the placement data without changed transforms (e.g.
/// the geospatial web viewer) leave the setting empty and append the scene
/// index themselves.
///
class UsdGeospatialImagingSceneIndexPlugin : public UsdImagingSceneIndexPlugin
{
public:
    USDGEOSPATIALIMAGING_API
    HdSceneIndexBaseRefPtr AppendSceneIndex(
        HdSceneIndexBaseRefPtr const &inputScene) override;

    USDGEOSPATIALIMAGING_API
    HdContainerDataSourceHandle FlattenedDataSourceProviders() override;

    USDGEOSPATIALIMAGING_API
    TfTokenVector InstanceDataSourceNames() override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
