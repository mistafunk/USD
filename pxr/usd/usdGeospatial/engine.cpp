//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/engine.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdGeospatialCoordinateOperationEngine>();
}

UsdGeospatialCoordinateOperationEngine::
UsdGeospatialCoordinateOperationEngine() = default;

UsdGeospatialCoordinateOperationEngine::
~UsdGeospatialCoordinateOperationEngine() = default;

UsdGeospatialEngineFactoryBase::~UsdGeospatialEngineFactoryBase() = default;

std::string
UsdGeospatialCoordinateOperationEngine::MakeCompoundCrs(
    const std::string&, const std::string&)
{
    return std::string();
}

PXR_NAMESPACE_CLOSE_SCOPE
