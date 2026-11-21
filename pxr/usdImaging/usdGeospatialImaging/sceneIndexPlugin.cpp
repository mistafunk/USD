//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdGeospatialImaging/sceneIndexPlugin.h"

#include "pxr/usdImaging/usdGeospatialImaging/bindingSchema.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSceneIndex.h"
#include "pxr/usdImaging/usdGeospatialImaging/tokens.h"

#include "pxr/imaging/hd/flattenedDataSourceProviders.h"
#include "pxr/imaging/hd/flattenedOverlayDataSourceProvider.h"
#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/stringUtils.h"

#include <cstdlib>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(
    USDGEOSPATIALIMAGING_OUTPUT_CRS, "",
    "Output CRS (e.g. EPSG:4979) in which usdImaging clients show "
    "geo-referenced models; empty to leave transforms unchanged.");

TF_DEFINE_ENV_SETTING(
    USDGEOSPATIALIMAGING_DISPLAY_ORIGIN, "",
    "Display origin \"x, y, z\" in the output CRS; empty for the origin of "
    "the first anchor placed.");

TF_REGISTRY_FUNCTION(UsdImagingSceneIndexPlugin)
{
    UsdImagingSceneIndexPlugin::Define<UsdGeospatialImagingSceneIndexPlugin>();
}

HdSceneIndexBaseRefPtr
UsdGeospatialImagingSceneIndexPlugin::AppendSceneIndex(
    HdSceneIndexBaseRefPtr const &inputScene)
{
    const std::string outputCrs =
        TfGetEnvSetting(USDGEOSPATIALIMAGING_OUTPUT_CRS);
    if (outputCrs.empty()) {
        return inputScene;
    }

    TfTokenVector names{
        UsdGeospatialImagingPlacementSceneIndexTokens->outputCrs,
        UsdGeospatialImagingPlacementSceneIndexTokens->applyToXform };
    std::vector<HdDataSourceBaseHandle> values{
        HdRetainedTypedSampledDataSource<std::string>::New(outputCrs),
        HdRetainedTypedSampledDataSource<bool>::New(true) };

    const std::string origin =
        TfGetEnvSetting(USDGEOSPATIALIMAGING_DISPLAY_ORIGIN);
    if (!origin.empty()) {
        const std::vector<std::string> parts =
            TfStringSplit(TfStringReplace(origin, " ", ""), ",");
        if (parts.size() == 3) {
            names.push_back(
                UsdGeospatialImagingPlacementSceneIndexTokens->displayOrigin);
            values.push_back(HdRetainedTypedSampledDataSource<GfVec3d>::New(
                GfVec3d(std::strtod(parts[0].c_str(), nullptr),
                        std::strtod(parts[1].c_str(), nullptr),
                        std::strtod(parts[2].c_str(), nullptr))));
        } else {
            TF_WARN("USDGEOSPATIALIMAGING_DISPLAY_ORIGIN '%s' is not "
                    "\"x, y, z\"; using the first anchor", origin.c_str());
        }
    }

    return UsdGeospatialImagingPlacementSceneIndex::New(
        inputScene,
        HdRetainedContainerDataSource::New(
            names.size(), names.data(), values.data()));
}

HdContainerDataSourceHandle
UsdGeospatialImagingSceneIndexPlugin::FlattenedDataSourceProviders()
{
    using namespace HdMakeDataSourceContainingFlattenedDataSourceProvider;

    return HdRetainedContainerDataSource::New(
        UsdGeospatialImagingBindingSchema::GetSchemaToken(),
        Make<HdFlattenedOverlayDataSourceProvider>());
}

TfTokenVector
UsdGeospatialImagingSceneIndexPlugin::InstanceDataSourceNames()
{
    // Instances placed through different anchors cannot share prototypes.
    return { UsdGeospatialImagingBindingSchema::GetSchemaToken() };
}

PXR_NAMESPACE_CLOSE_SCOPE
