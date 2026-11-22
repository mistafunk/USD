//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_USD_GEOSPATIAL_ENGINE_REGISTRY_H
#define PXR_USD_USD_GEOSPATIAL_ENGINE_REGISTRY_H

/// \file usdGeospatial/engineRegistry.h

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/usd/usdGeospatial/engine.h"
#include "pxr/base/tf/singleton.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdGeospatialEngineRegistry
///
/// Finds the coordinate operation engines declared by plugins and selects
/// one. Engines are created on first use and kept until ResetEngines().
///
/// The active engine is, in order of precedence:
/// - the engine set with SetActiveEngine(),
/// - the engine named by the USDGEOSPATIAL_ENGINE environment setting,
/// - the engine with the highest \c priority in its plugInfo.json, by
///   name for equal priorities.
///
class UsdGeospatialEngineRegistry
{
public:
    USDGEOSPATIAL_API
    static UsdGeospatialEngineRegistry& GetInstance();

    UsdGeospatialEngineRegistry(const UsdGeospatialEngineRegistry&) = delete;
    UsdGeospatialEngineRegistry& operator=(
        const UsdGeospatialEngineRegistry&) = delete;

    /// Names of all engines declared by plugins, sorted.
    USDGEOSPATIAL_API
    std::vector<std::string> GetEngineNames() const;

    /// Name of the engine GetEngine() returns without an argument, or an
    /// empty string if there is none.
    USDGEOSPATIAL_API
    std::string GetActiveEngineName() const;

    /// Makes \p name the active engine. An empty name restores the
    /// default selection. Returns false, with an error, for an unknown
    /// name.
    USDGEOSPATIAL_API
    bool SetActiveEngine(const std::string& name);

    /// Returns the engine called \p name, or the active engine if \p name
    /// is empty. Loads its plugin if needed. Returns null, with an error,
    /// if there is no such engine or it cannot be created.
    USDGEOSPATIAL_API
    UsdGeospatialCoordinateOperationEngineSharedPtr GetEngine(
        const std::string& name = std::string());

    /// Drops the engine instances, so GetEngine() creates new ones, which
    /// see data that became available since (e.g. grid files a result
    /// reported missing). Clients holding an engine keep using it.
    USDGEOSPATIAL_API
    void ResetEngines();

private:
    friend class TfSingleton<UsdGeospatialEngineRegistry>;
    UsdGeospatialEngineRegistry();

    struct _Entry {
        TfType type;
        int priority = 0;
        UsdGeospatialCoordinateOperationEngineSharedPtr engine;
    };

    std::string _GetActiveEngineName() const;

    mutable std::mutex _mutex;
    std::map<std::string, _Entry> _entries;
    std::string _explicitActive;
};

USDGEOSPATIAL_API_TEMPLATE_CLASS(TfSingleton<UsdGeospatialEngineRegistry>);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USD_GEOSPATIAL_ENGINE_REGISTRY_H
