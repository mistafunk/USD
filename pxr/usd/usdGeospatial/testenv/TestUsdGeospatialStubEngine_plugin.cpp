//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/engine.h"

PXR_NAMESPACE_USING_DIRECTIVE

// A second engine for the registry tests. It understands no CRS and
// performs no operations; it only has to be found, selected and named.
class TestUsdGeospatialStubEngine
    : public UsdGeospatialCoordinateOperationEngine
{
public:
    std::string GetName() const override { return "stub"; }

    UsdGeospatialCrsInfo DescribeCrs(const std::string&) override
    {
        UsdGeospatialCrsInfo info;
        info.error = "the stub engine understands no CRS";
        return info;
    }

    UsdGeospatialOperationResult CanTransform(
        const std::string&, const std::string&,
        const UsdGeospatialAreaOfInterest&) override
    {
        UsdGeospatialOperationResult result;
        result.engine = GetName();
        result.error = "the stub engine performs no operations";
        return result;
    }

    UsdGeospatialOperationResult Transform(
        const std::string& source, const std::string& target,
        const UsdGeospatialAreaOfInterest& area,
        std::vector<GfVec3d>*) override
    {
        return CanTransform(source, target, area);
    }
};

USDGEOSPATIAL_DEFINE_ENGINE(TestUsdGeospatialStubEngine);
