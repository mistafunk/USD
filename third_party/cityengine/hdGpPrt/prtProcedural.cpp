#include "prtProcedural.h"

#include "pxr/base/tf/diagnostic.h"

#include "pxr/imaging/hdGp/generativeProceduralPlugin.h"
#include "pxr/imaging/hdGp/generativeProceduralPluginRegistry.h"

#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

}

TF_DEFINE_PRIVATE_TOKENS(prtTokens, (sourceMeshPath)(rpkPath));

class PrtProcedural : public HdGpGenerativeProcedural {
public:
    PrtProcedural(const SdfPath &proceduralPrimPath)
            : HdGpGenerativeProcedural(proceduralPrimPath) {
    }

    virtual ~PrtProcedural() = default;

    DependencyMap
    UpdateDependencies(const HdSceneIndexBaseRefPtr &inputScene) override {
        DependencyMap result;
        _Args args = _GetArgs(inputScene);
        if (!args.sourceMeshPath.IsEmpty()) {
            result[args.sourceMeshPath] = HdPrimvarsSchema::GetDefaultLocator();
        }
        return result;
    }

    ChildPrimTypeMap Update(
            const HdSceneIndexBaseRefPtr &inputScene,
            const ChildPrimTypeMap &previousResult,
            const DependencyMap &dirtiedDependencies,
            HdSceneIndexObserver::DirtiedPrimEntries *outputDirtiedPrims) override {
        TF_STATUS("PrtProcedural::Update");

        ChildPrimTypeMap result;

        _Args args = _GetArgs(inputScene);
        if (args.sourceMeshPath.IsEmpty()) {
            return result;
        }

        // TODO: check if any work is needed at all compared to previous run
        //       see pxr/usdImaging/usdImagingGL/testenv/TestUsdImagingGLHdGpProcedurals.cpp

        // collect source meshes for PRT initial shapes
        std::vector<std::pair<HdMeshSchema, TfToken>> sourceMeshes;
        HdSceneIndexPrim sourceMeshPrim = inputScene->GetPrim(args.sourceMeshPath);
        if (sourceMeshPrim.primType == HdPrimTypeTokens->mesh) {
            TF_STATUS("source mesh path: %s%", args.sourceMeshPath.GetText());
            HdMeshSchema mesh = HdMeshSchema::GetFromParent(sourceMeshPrim.dataSource);
            if (mesh.IsDefined()) {
                sourceMeshes.emplace_back(mesh, args.sourceMeshPath.GetNameToken());
            }
        }
        if (sourceMeshes.empty())
            return result;

        // TODO: convert source meshes into prt initial shapes

        // TODO: setup PRT callbacks and encoder options

        // TODO: run prt::generate

        // TODO: convert PRT geometry in callbacks to Hd data sources

        // TODO: create one child mesh prim per source mesh
        SdfPath prtPrimPath = _GetProceduralPrimPath();
        for (auto mesh: sourceMeshes) { // TODO: use successfully generated PRT models
            SdfPath prtChildPath = prtPrimPath.AppendChild(mesh.second);
            result[prtChildPath] = HdPrimTypeTokens->mesh;
        }
        return result;
    }

    // called concurrently from multiple threads
    HdSceneIndexPrim GetChildPrim(const HdSceneIndexBaseRefPtr &inputScene,
                                  const SdfPath &childPrimPath) override {
        TF_STATUS("PrtProcedural::GetChildPrim");

        HdSceneIndexPrim result;

        result.primType = HdPrimTypeTokens->mesh;
        result.dataSource = HdRetainedContainerDataSource::New(
                HdMeshSchemaTokens->mesh, _GetChildMeshDs(),
                HdPrimvarsSchemaTokens->primvars, _GetChildPrimvarsDs());

        return result;
    }

private:
    struct _Args {
        _Args() = default;

        SdfPath sourceMeshPath;
        std::string rpkPath; // TODO: should be an asset path
    };

    _Args _GetArgs(const HdSceneIndexBaseRefPtr &inputScene) {
        _Args result;

        HdSceneIndexPrim myPrim = inputScene->GetPrim(_GetProceduralPrimPath());

        HdPrimvarsSchema primvars =
                HdPrimvarsSchema::GetFromParent(myPrim.dataSource);

        if (HdSampledDataSourceHandle sourceMeshDs =
                primvars.GetPrimvar(prtTokens->sourceMeshPath).GetPrimvarValue()) {
            VtValue v = sourceMeshDs->GetValue(0.0f);

            if (v.IsHolding<VtArray<SdfPath>>()) {
                VtArray<SdfPath> a = v.UncheckedGet<VtArray<SdfPath>>();
                if (a.size() == 1) {
                    result.sourceMeshPath = a[0];
                }
            }
        }

        if (HdSampledDataSourceHandle ds =
                primvars.GetPrimvar(prtTokens->rpkPath).GetPrimvarValue()) {
            VtValue v = ds->GetValue(0.0f);
            if (v.IsHolding<std::string>()) {
                result.rpkPath = v.UncheckedGet<std::string>();
            }
        }

        return result;
    }

    HdContainerDataSourceHandle _GetChildMeshDs() {
        static const VtIntArray faceVertexCounts = {4, 4, 4, 4, 4, 4};

        static const VtIntArray faceVertexIndices = {
                0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4};

        using _IntArrayDs = HdRetainedTypedSampledDataSource<VtIntArray>;

        static const _IntArrayDs::Handle fvcDs = _IntArrayDs::New(faceVertexCounts);

        static const _IntArrayDs::Handle fviDs =
                _IntArrayDs::New(faceVertexIndices);

        static const HdContainerDataSourceHandle meshDs =
                HdMeshSchema::Builder()
                        .SetTopology(HdMeshTopologySchema::Builder()
                                             .SetFaceVertexCounts(fvcDs)
                                             .SetFaceVertexIndices(fviDs)
                                             .Build())
                        .Build();

        return meshDs;
    }

    HdContainerDataSourceHandle _GetChildPrimvarsDs() {
        static const VtArray<GfVec3f> points = {
                {-0.1f, -0.1f, 0.1f},
                {0.1f,  -0.1f, 0.1f},
                {-0.1f, 0.1f,  0.1f},
                {0.1f,  0.1f,  0.1f},
                {-0.1f, 0.1f,  -0.1f},
                {0.1f,  0.1f,  -0.1f},
                {-0.1f, -0.1f, -0.1f},
                {0.1f,  -0.1f, -0.1f}};

        using _PointArrayDs = HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>;

        static const HdContainerDataSourceHandle primvarsDs =
                HdRetainedContainerDataSource::New(
                        HdPrimvarsSchemaTokens->points,
                        HdPrimvarSchema::Builder()
                                .SetPrimvarValue(_PointArrayDs::New(points))
                                .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
                                        HdPrimvarSchemaTokens->vertex))
                                .SetRole(HdPrimvarSchema::BuildRoleDataSource(
                                        HdPrimvarSchemaTokens->point))
                                .Build());

        return primvarsDs;
    }
};

class PrtProceduralPlugin : public HdGpGenerativeProceduralPlugin {
public:
    HdGpGenerativeProcedural *
    Construct(const SdfPath &proceduralPrimPath) override {
        return new PrtProcedural(proceduralPrimPath);
    }
};

TF_REGISTRY_FUNCTION(TfType) {
    HdGpGenerativeProceduralPluginRegistry::Define<
            PrtProceduralPlugin, HdGpGenerativeProceduralPlugin>();
}
