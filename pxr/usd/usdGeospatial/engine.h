//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_USD_GEOSPATIAL_ENGINE_H
#define PXR_USD_USD_GEOSPATIAL_ENGINE_H

/// \file usdGeospatial/engine.h

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \struct UsdGeospatialOperationResult
///
/// The outcome of a coordinate operation request. Every result names the
/// engine and the operation used, with its accuracy, or says why the
/// operation is not possible; an engine never guesses.
///
struct UsdGeospatialOperationResult
{
    bool succeeded = false;

    /// Name of the engine that produced the result.
    std::string engine;

    /// Description of the coordinate operation used, as the engine names it.
    std::string operation;

    /// Accuracy of the operation in metres, or a negative value if unknown.
    double accuracy = -1.0;

    /// Why the operation failed, if it did.
    std::string error;

    explicit operator bool() const { return succeeded; }
};

/// \struct UsdGeospatialCrsInfo
///
/// What an engine reports about a CRS definition: enough for placement to
/// check the coordinate tuple and to build the source datum's
/// east/north/up frame, without parsing WKT itself.
///
struct UsdGeospatialCrsInfo
{
    bool valid = false;

    /// Why the definition could not be used, if it could not.
    std::string error;

    /// The CRS name from its definition.
    std::string name;

    /// Number of coordinate axes, including the vertical one of a compound
    /// CRS.
    int dimension = 0;

    /// True for a geographic CRS (longitude and latitude on an ellipsoid).
    bool isGeographic = false;

    /// True for a geocentric (earth-centred Cartesian) CRS.
    bool isGeocentric = false;

    /// Axis directions in definition order, as named by ISO 19111 / WKT2,
    /// e.g. "east", "north", "up", "geocentricX".
    std::vector<TfToken> axisDirections;

    /// Semi-major axis (metres) and inverse flattening of the datum's
    /// ellipsoid.
    double semiMajorAxis = 0.0;
    double inverseFlattening = 0.0;

    /// The datum's 3D geographic CRS (longitude, latitude, ellipsoidal
    /// height), as a definition the engine accepts.
    std::string geographic3D;
};

/// \class UsdGeospatialCoordinateOperationEngine
///
/// Interface for coordinate operation engines, the plugins that convert and
/// transform coordinates between CRS definitions (for example PROJ).
/// usdGeospatial itself contains no engine.
///
/// CRS definitions are strings the engine accepts: OGC WKT2 from
/// \c crs:wkt, and whatever else an engine documents (such as
/// "EPSG:4979"). Coordinates are passed in the proposal's semantic tuple
/// order, independent of the definition's axis order: easting, northing,
/// up for projected and other east/north/up Cartesian CRSs; longitude,
/// latitude, height for geographic CRSs; X, Y, Z for geocentric CRSs.
///
/// Calls are synchronous. An engine must be safe to call from several
/// threads.
///
/// Engines are found through the plugin system. A plugin declares its
/// engine type in plugInfo.json with this class as a base, a unique
/// \c engineName and an optional integer \c priority:
///
/// \code
/// "Types": {
///     "MyEngine": {
///         "bases": ["UsdGeospatialCoordinateOperationEngine"],
///         "engineName": "my",
///         "priority": 100
///     }
/// }
/// \endcode
///
/// and defines it with USDGEOSPATIAL_DEFINE_ENGINE(MyEngine). See
/// UsdGeospatialEngineRegistry for how an engine is selected.
///
class UsdGeospatialCoordinateOperationEngine
{
public:
    USDGEOSPATIAL_API
    virtual ~UsdGeospatialCoordinateOperationEngine();

    UsdGeospatialCoordinateOperationEngine(
        const UsdGeospatialCoordinateOperationEngine&) = delete;
    UsdGeospatialCoordinateOperationEngine& operator=(
        const UsdGeospatialCoordinateOperationEngine&) = delete;

    /// The engine's name, as declared in its plugInfo.json.
    virtual std::string GetName() const = 0;

    /// Describes the CRS \p definition.
    virtual UsdGeospatialCrsInfo DescribeCrs(
        const std::string& definition) = 0;

    /// Checks that an operation from \p source to \p target exists, without
    /// transforming anything, and reports the operation it would use.
    virtual UsdGeospatialOperationResult CanTransform(
        const std::string& source, const std::string& target) = 0;

    /// Transforms \p points in place from \p source to \p target. On
    /// failure \p points is unspecified and the result says why.
    virtual UsdGeospatialOperationResult Transform(
        const std::string& source, const std::string& target,
        std::vector<GfVec3d>* points) = 0;

protected:
    USDGEOSPATIAL_API
    UsdGeospatialCoordinateOperationEngine();
};

using UsdGeospatialCoordinateOperationEngineSharedPtr =
    std::shared_ptr<UsdGeospatialCoordinateOperationEngine>;

/// \class UsdGeospatialEngineFactoryBase
/// \private
class UsdGeospatialEngineFactoryBase : public TfType::FactoryBase
{
public:
    USDGEOSPATIAL_API
    ~UsdGeospatialEngineFactoryBase() override;

    virtual UsdGeospatialCoordinateOperationEngine* New() const = 0;
};

/// \class UsdGeospatialEngineFactory
/// \private
template <class T>
class UsdGeospatialEngineFactory : public UsdGeospatialEngineFactoryBase
{
public:
    UsdGeospatialCoordinateOperationEngine* New() const override
    {
        return new T;
    }
};

/// \private
template <class Engine>
void UsdGeospatial_DefineEngine()
{
    TfType::Define<Engine,
                   TfType::Bases<UsdGeospatialCoordinateOperationEngine>>()
        .template SetFactory<UsdGeospatialEngineFactory<Engine>>();
}

/// Defines \p EngineClass as a coordinate operation engine with the TfType
/// system, so the engine registry can create it. Use once, in the engine's
/// source file.
#define USDGEOSPATIAL_DEFINE_ENGINE(EngineClass)        \
TF_REGISTRY_FUNCTION(TfType) {                          \
    UsdGeospatial_DefineEngine<EngineClass>();          \
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USD_GEOSPATIAL_ENGINE_H
