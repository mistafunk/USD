//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/engineRegistry.h"

#include "pxr/base/js/value.h"
#include "pxr/base/js/utils.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/instantiateSingleton.h"

#include <algorithm>
#include <set>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(UsdGeospatialEngineRegistry);

TF_DEFINE_ENV_SETTING(
    USDGEOSPATIAL_ENGINE, "",
    "Name of the coordinate operation engine usdGeospatial uses, as "
    "declared by the engine plugin's engineName. Empty: the engine with the "
    "highest priority.");

UsdGeospatialEngineRegistry&
UsdGeospatialEngineRegistry::GetInstance()
{
    return TfSingleton<UsdGeospatialEngineRegistry>::GetInstance();
}

UsdGeospatialEngineRegistry::UsdGeospatialEngineRegistry()
{
    TfSingleton<UsdGeospatialEngineRegistry>::SetInstanceConstructed(*this);

    std::set<TfType> types;
    PlugRegistry::GetAllDerivedTypes<UsdGeospatialCoordinateOperationEngine>(
        &types);

    for (const TfType& type : types) {
        const PlugPluginPtr plugin =
            PlugRegistry::GetInstance().GetPluginForType(type);
        if (!plugin) {
            continue;
        }
        const JsObject metadata = plugin->GetMetadataForType(type);

        std::string name;
        if (const JsOptionalValue v = JsFindValue(metadata, "engineName")) {
            if (v->IsString()) {
                name = v->GetString();
            }
        }
        if (name.empty()) {
            TF_WARN("Coordinate operation engine %s declares no engineName "
                    "in plugin '%s'; ignoring it.",
                    type.GetTypeName().c_str(), plugin->GetName().c_str());
            continue;
        }

        int priority = 0;
        if (const JsOptionalValue v = JsFindValue(metadata, "priority")) {
            if (v->IsInt()) {
                priority = v->GetInt();
            }
        }

        if (_entries.count(name)) {
            TF_WARN("Coordinate operation engine name '%s' is declared by "
                    "both %s and %s; using the first.", name.c_str(),
                    _entries[name].type.GetTypeName().c_str(),
                    type.GetTypeName().c_str());
            continue;
        }
        _entries[name] = _Entry{type, priority, nullptr};
    }
}

std::vector<std::string>
UsdGeospatialEngineRegistry::GetEngineNames() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::string> names;
    for (const auto& entry : _entries) {
        names.push_back(entry.first);
    }
    return names;
}

std::string
UsdGeospatialEngineRegistry::_GetActiveEngineName() const
{
    if (!_explicitActive.empty()) {
        return _explicitActive;
    }

    const std::string fromEnv = TfGetEnvSetting(USDGEOSPATIAL_ENGINE);
    if (!fromEnv.empty()) {
        if (_entries.count(fromEnv)) {
            return fromEnv;
        }
        TF_WARN("USDGEOSPATIAL_ENGINE names unknown engine '%s'; using the "
                "default selection.", fromEnv.c_str());
    }

    // Highest priority wins; std::map order breaks ties by name.
    const _Entry* best = nullptr;
    std::string bestName;
    for (const auto& entry : _entries) {
        if (!best || entry.second.priority > best->priority) {
            best = &entry.second;
            bestName = entry.first;
        }
    }
    return bestName;
}

std::string
UsdGeospatialEngineRegistry::GetActiveEngineName() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _GetActiveEngineName();
}

bool
UsdGeospatialEngineRegistry::SetActiveEngine(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!name.empty() && !_entries.count(name)) {
        TF_CODING_ERROR("Unknown coordinate operation engine '%s'.",
                        name.c_str());
        return false;
    }
    _explicitActive = name;
    return true;
}

UsdGeospatialCoordinateOperationEngineSharedPtr
UsdGeospatialEngineRegistry::GetEngine(const std::string& requested)
{
    std::lock_guard<std::mutex> lock(_mutex);

    const std::string name =
        requested.empty() ? _GetActiveEngineName() : requested;
    if (name.empty()) {
        TF_RUNTIME_ERROR("No coordinate operation engine is available.");
        return nullptr;
    }
    auto it = _entries.find(name);
    if (it == _entries.end()) {
        TF_CODING_ERROR("Unknown coordinate operation engine '%s'.",
                        name.c_str());
        return nullptr;
    }

    _Entry& entry = it->second;
    if (!entry.engine) {
        const PlugPluginPtr plugin =
            PlugRegistry::GetInstance().GetPluginForType(entry.type);
        if (!plugin || !plugin->Load()) {
            TF_RUNTIME_ERROR("Failed to load the plugin for coordinate "
                             "operation engine '%s'.", name.c_str());
            return nullptr;
        }
        const UsdGeospatialEngineFactoryBase* factory =
            entry.type.GetFactory<UsdGeospatialEngineFactoryBase>();
        if (!factory) {
            TF_CODING_ERROR("Coordinate operation engine %s has no factory; "
                            "define it with USDGEOSPATIAL_DEFINE_ENGINE.",
                            entry.type.GetTypeName().c_str());
            return nullptr;
        }
        entry.engine.reset(factory->New());
    }
    return entry.engine;
}

PXR_NAMESPACE_CLOSE_SCOPE
