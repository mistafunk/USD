//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Engine discovery and selection. Run with USDGEOSPATIAL_ENGINE=stub: the
// stub test engine must be selected over the higher-priority PROJ engine,
// and every result must name the engine that produced it.

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/engineRegistry.h"
#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/base/arch/systemInfo.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"

#include <algorithm>
#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

int
main()
{
    // The build installs the stub engine plugin next to this test.
    const std::string stubPath = TfStringCatPaths(
        TfGetPathName(ArchGetExecutablePath()),
        "UsdGeospatialPlugins/lib/TestUsdGeospatialStubEngine*/Resources/")
        + "/";
    TF_AXIOM(PlugRegistry::GetInstance().RegisterPlugins(stubPath).size() == 1);

    UsdGeospatialEngineRegistry& registry =
        UsdGeospatialEngineRegistry::GetInstance();
    const std::vector<std::string> names = registry.GetEngineNames();
    TF_AXIOM(std::find(names.begin(), names.end(), "stub") != names.end());
    const bool hasProj =
        std::find(names.begin(), names.end(), "proj") != names.end();

    // USDGEOSPATIAL_ENGINE=stub overrides the priority order.
    TF_AXIOM(registry.GetActiveEngineName() == "stub");
    UsdGeospatialCoordinateOperationEngineSharedPtr engine =
        registry.GetEngine();
    TF_AXIOM(engine && engine->GetName() == "stub");

    // Results name the engine; the stub cannot place anything.
    UsdGeospatialPlacementQuery stubQuery("EPSG:4979");
    TF_AXIOM(!stubQuery.IsValid());
    TF_AXIOM(TfStringContains(stubQuery.GetError(), "stub"));
    const UsdGeospatialOperationResult op =
        engine->CanTransform("EPSG:4326", "EPSG:4979");
    TF_AXIOM(!op && op.engine == "stub" && !op.error.empty());

    // An explicit selection wins over the environment; empty restores it.
    if (hasProj) {
        TF_AXIOM(registry.SetActiveEngine("proj"));
        TF_AXIOM(registry.GetActiveEngineName() == "proj");
        TF_AXIOM(registry.GetEngine()->GetName() == "proj");
        TF_AXIOM(registry.SetActiveEngine(""));
        TF_AXIOM(registry.GetActiveEngineName() == "stub");
    }

    // Unknown names are rejected.
    {
        TfErrorMark mark;
        TF_AXIOM(!registry.SetActiveEngine("doesNotExist"));
        TF_AXIOM(!registry.GetEngine("doesNotExist"));
        TF_AXIOM(!mark.IsClean());
        mark.Clear();
    }

    printf("OK (engines: %s)\n", TfStringJoin(names, ", ").c_str());
    return 0;
}
