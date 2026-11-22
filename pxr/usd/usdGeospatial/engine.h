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

    /// More accurate operations that were not used because they need data
    /// the engine does not have (e.g. grid files), as the engine names them,
    /// with what is missing. Empty if the best known operation was used.
    std::vector<std::string> skippedOperations;

    /// The data files (e.g. grid files, by name) that the skipped
    /// operations, or the unavailable ones when the request failed, need.
    /// A client that can provide them may retry with a new engine instance
    /// (UsdGeospatialEngineRegistry::ResetEngines).
    std::vector<std::string> missingGrids;

    explicit operator bool() const { return succeeded; }
};

/// \struct UsdGeospatialAreaOfInterest
///
/// Where an operation will be used, as geographic bounds in degrees, so an
/// engine can choose among regional operations: many datum transformations
/// are only valid, or only best, in part of their datum's extent. An empty
/// area means anywhere.
///
struct UsdGeospatialAreaOfInterest
{
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;

    bool IsEmpty() const { return !(west <= east && south <= north) ||
                                  (west == 0.0 && east == 0.0 &&
                                   south == 0.0 && north == 0.0); }

    /// A small area around one point (longitude, latitude in degrees).
    static UsdGeospatialAreaOfInterest AtPoint(double lon, double lat)
    {
        const double d = 1e-6;
        return {lon - d, lat - d, lon + d, lat + d};
    }
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

    /// For a 2D geographic or projected CRS: the same CRS with an
    /// ellipsoidal height axis, as a definition the engine accepts. Empty
    /// for 3D CRSs and if the engine cannot derive it.
    std::string promoted3D;
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

    /// Checks that an operation from \p source to \p target exists for
    /// \p area, without transforming anything, and reports the operation it
    /// would use.
    virtual UsdGeospatialOperationResult CanTransform(
        const std::string& source, const std::string& target,
        const UsdGeospatialAreaOfInterest& area) = 0;

    /// Transforms \p points in place from \p source to \p target with the
    /// operation CanTransform() reports for \p area. On failure \p points
    /// is unspecified and the result says why.
    virtual UsdGeospatialOperationResult Transform(
        const std::string& source, const std::string& target,
        const UsdGeospatialAreaOfInterest& area,
        std::vector<GfVec3d>* points) = 0;

    /// A compound CRS of the 2D \p horizontal CRS and the \p vertical CRS
    /// (e.g. "EPSG:3855", EGM2008 height), as a definition the engine
    /// accepts; empty if the engine cannot compose CRSs. Placement uses it
    /// for the proposal's default height of 2D CRSs.
    USDGEOSPATIAL_API
    virtual std::string MakeCompoundCrs(const std::string& horizontal,
                                        const std::string& vertical);

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
