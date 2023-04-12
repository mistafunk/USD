#include "prtCallbacks.h"
#include "prtContext.h"
#include "prtHydraEncoder.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/fileUtils.h"

#include "pxr/imaging/hdGp/generativeProceduralPlugin.h"
#include "pxr/imaging/hdGp/generativeProceduralPluginRegistry.h"

#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/usd/ar/packageUtils.h"

#include "prt/API.h"
#include "prt/AttributeMap.h"
#include "prt/EncoderInfo.h"
#include "prt/InitialShape.h"
#include "prt/Object.h"
#include "prt/OcclusionSet.h"
#include "prt/RuleFileInfo.h"

#include "prtx/ExtensionManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

constexpr const wchar_t* ENC_ID_ATTR_EVAL = L"com.esri.prt.core.AttributeEvalEncoder";
constexpr const wchar_t* ENC_ID_CGA_ERROR = L"com.esri.prt.core.CGAErrorEncoder";
constexpr const wchar_t* ENC_ID_CGA_PRINT = L"com.esri.prt.core.CGAPrintEncoder";

std::unique_ptr<PRTContext> prtContext; // TODO: avoid global variable

} // namespace

TF_DEFINE_PRIVATE_TOKENS(prtTokens, (sourceMeshPath)(rpkPath));

class PrtProcedural : public HdGpGenerativeProcedural {
public:
	PrtProcedural(const SdfPath& proceduralPrimPath)
	    : HdGpGenerativeProcedural(proceduralPrimPath) {}

	virtual ~PrtProcedural() = default;

	DependencyMap UpdateDependencies(const HdSceneIndexBaseRefPtr& inputScene) override {
		DependencyMap result;
		_Args args = _GetArgs(inputScene);
		if (!args.sourceMeshPath.IsEmpty()) {
			result[args.sourceMeshPath] = HdPrimvarsSchema::GetDefaultLocator();
		}
		return result;
	}

	ChildPrimTypeMap Update(const HdSceneIndexBaseRefPtr& inputScene,
	                        const ChildPrimTypeMap& previousResult,
	                        const DependencyMap& dirtiedDependencies,
	                        HdSceneIndexObserver::DirtiedPrimEntries* outputDirtiedPrims) override {
		ChildPrimTypeMap result;

		_Args args = _GetArgs(inputScene);
		TF_STATUS("PrtProcedural::Update:\n   source mesh: %s%\n   rpk: %s%",
		          args.sourceMeshPath.GetText(), args.rpkPath.GetResolvedPath().c_str());

		if (args.sourceMeshPath.IsEmpty()) {
			mGeneratedDataSourceHandle.reset();
			return result;
		}

		HdSceneIndexPrim sourceMeshPrim = inputScene->GetPrim(args.sourceMeshPath);
		if (sourceMeshPrim.primType != HdPrimTypeTokens->mesh) {
			mGeneratedDataSourceHandle.reset();
			return result;
		}

//		if (mGeneratedDataSourceHandle) {
//			if (dirtiedDependencies.find())
//		}

		TF_STATUS("source mesh path: %s%", args.sourceMeshPath.GetText());
		HdMeshSchema sourceMeshSchema = HdMeshSchema::GetFromParent(sourceMeshPrim.dataSource);
		if (!sourceMeshSchema)
			return result;
		TfToken sourcePrimName = args.sourceMeshPath.GetNameToken();

		HdPrimvarsSchema primvarsSchema =
		        HdPrimvarsSchema::GetFromParent(sourceMeshPrim.dataSource);
		HdPrimvarSchema pointsPrimvarSchema =
		        primvarsSchema.GetPrimvar(HdPrimvarsSchemaTokens->points);
		HdSampledDataSourceHandle pointsHandle = pointsPrimvarSchema.GetPrimvarValue();
		if (!pointsHandle)
			return result;
		VtValue pointsValue = pointsHandle->GetValue(0.0f);
		if (!pointsValue.IsHolding<VtArray<GfVec3f>>())
			return result;
		const VtArray<GfVec3f>& points = pointsValue.UncheckedGet<VtArray<GfVec3f>>();

		HdMeshTopologySchema topo = sourceMeshSchema.GetTopology();
		VtArray<int> faceCounts = topo.GetFaceVertexCounts()->GetTypedValue(0);
		VtArray<int> faceVertexIndices = topo.GetFaceVertexIndices()->GetTypedValue(0);

		std::vector<double> prtPoints;
		prtPoints.reserve(points.size() * 3);
		for (const GfVec3f& v : points) {
			prtPoints.push_back(v[0]);
			prtPoints.push_back(v[1]);
			prtPoints.push_back(v[2]);
		}

		// TODO: avoid these copies
		std::vector<uint32_t> prtIndices(faceVertexIndices.begin(), faceVertexIndices.end());
		std::vector<uint32_t> prtCounts(faceCounts.begin(), faceCounts.end());

		InitialShapeBuilderUPtr isb(prt::InitialShapeBuilder::create());
		const prt::Status setGeoStatus =
		        isb->setGeometry(prtPoints.data(), prtPoints.size(), prtIndices.data(),
		                         prtIndices.size(), prtCounts.data(), prtCounts.size());
		if (setGeoStatus != prt::STATUS_OK) {
			LOG_ERR << "InitialShapeBuilder setGeometry failed with status = "
			        << prt::getStatusDescription(setGeoStatus);
			return result;
		}

		const std::string resolvedRpkPath = ArchNormPath(args.rpkPath.GetResolvedPath());
		LOG_DBG << "resolved RPK path: " << resolvedRpkPath;

		ResolveMapUPtr resolveMap;
		if (ArIsPackageRelativePath(resolvedRpkPath)) {
			auto [packagePath, packagedPath] = ArSplitPackageRelativePathOuter(resolvedRpkPath);
			LOG_DBG << "detected RPK within USDZ package: " << packagePath;

			// workaround for PRT limitation: PRT does not recursively create a resolvemap from RPK within USDZ
			// TODO: cleanup/cache unpack location
			std::string tmpDir(ArchGetTmpDir());
			std::string subTmpDir = ArchMakeTmpSubdir(tmpDir, "hdGpPrt");
			std::wstring unpackTmpDir = prtu::toUTF16FromUTF8(subTmpDir);
			const std::wstring packageFileUri = prtu::toFileURIFromUtf8String(packagePath);
			ResolveMapUPtr packageResolveMap(prt::createResolveMap(packageFileUri.c_str(), unpackTmpDir.c_str()));

			const std::wstring outerUriPath = prtu::toUTF16FromUTF8(packagedPath);
			resolveMap.reset(prt::createResolveMap(packageResolveMap->getString(outerUriPath.c_str())));
		}
		else {
			const std::wstring rpkURI = prtu::toFileURIFromUtf8String(resolvedRpkPath);
			resolveMap.reset(prt::createResolveMap(rpkURI.c_str(), nullptr));
		}

		if (!resolveMap)
			return result;

		LOG_DBG << prtu::objectToXML(resolveMap.get());

		const std::wstring ruleFileKey = prtu::getRuleFileEntry(*resolveMap);
		const wchar_t* ruleFileUri = resolveMap->getString(ruleFileKey.c_str());
		if (ruleFileUri == nullptr) {
			LOG_ERR << "Unable to find ruleFileUri, aborting.";
			return result;
		}

		RuleFileInfoUPtr ruleFileInfo(prt::createRuleFileInfo(ruleFileUri));
		const std::wstring startRule = prtu::detectStartRule(ruleFileInfo);

		HdSceneIndexPrim myPrim = inputScene->GetPrim(_GetProceduralPrimPath());
		HdPrimvarsSchema primvars = HdPrimvarsSchema::GetFromParent(myPrim.dataSource);
		AttributeMapBuilderUPtr amb(prt::AttributeMapBuilder::create());
		for (const auto& primvarName: primvars.GetPrimvarNames()) {
			HdPrimvarSchema primvarSchema = primvars.GetPrimvar(primvarName);
			HdSampledDataSourceHandle dataSourceHandle = primvarSchema.GetPrimvarValue();
			VtValue value = dataSourceHandle->GetValue(0);
			LOG_DBG << "attr: " << primvarName.GetString() << ", type: " <<	value.GetTypeName();
			if (value.IsHolding<float>()) {
				LOG_DBG << "    value: " << value.UncheckedGet<float>();
				amb->setFloat(prtu::toUTF16FromUTF8(primvarName.GetString()).c_str(), value.UncheckedGet<float>());
			}
		}
		AttributeMapUPtr initialShapeAttributes(amb->createAttributeMapAndReset());
		isb->setAttributes(ruleFileKey.c_str(), startRule.c_str(), 0, L"sourceMesh",
		                   initialShapeAttributes.get(), resolveMap.get());

		InitialShapeUPtr initialShape(isb->createInitialShapeAndReset());

		AttributeMapUPtr hydraEncOpts = prtu::createValidatedOptions(ENC_ID_HYDRA);
		const std::vector<const wchar_t*> encIDs = {ENC_ID_HYDRA};
		const AttributeMapNOPtrVector encOpts = {hydraEncOpts.get()};
		assert(encIDs.size() == encOpts.size());

		std::unique_ptr<PrtCallbacks> outputHandler(
		        new PrtCallbacks(mGeneratedDataSourceHandle, amb));
		InitialShapeNOPtrVector initialShapes = {initialShape.get()};
		const prt::Status generateStatus = prt::generate(
		        initialShapes.data(), initialShapes.size(), nullptr, encIDs.data(), encIDs.size(),
		        encOpts.data(), outputHandler.get(), prtContext->mPRTCache.get(), nullptr);
		if (generateStatus != prt::STATUS_OK) {
			LOG_ERR << "PRT generate failed with status = "
			        << prt::getStatusDescription(generateStatus);
			return result;
		}

		SdfPath prtPrimPath = _GetProceduralPrimPath();
		SdfPath prtChildPath = prtPrimPath.AppendChild(sourcePrimName);
		result[prtChildPath] = HdPrimTypeTokens->mesh;

		if (outputDirtiedPrims != nullptr) {
			outputDirtiedPrims->emplace_back(prtChildPath, HdMeshSchema::GetDefaultLocator());
		}

		return result;
	}

	// called concurrently from multiple threads
	HdSceneIndexPrim GetChildPrim(const HdSceneIndexBaseRefPtr& inputScene,
	                              const SdfPath& childPrimPath) override {
		HdSceneIndexPrim result;
		if (mGeneratedDataSourceHandle) {
			result.primType = HdPrimTypeTokens->mesh;
			result.dataSource = mGeneratedDataSourceHandle;
		}
		return result;
	}

private:
	HdContainerDataSourceHandle mGeneratedDataSourceHandle;

	struct _Args {
		_Args() = default;

		SdfPath sourceMeshPath;
		SdfAssetPath rpkPath;
	};

	_Args _GetArgs(const HdSceneIndexBaseRefPtr& inputScene) {
		_Args result;

		HdSceneIndexPrim myPrim = inputScene->GetPrim(_GetProceduralPrimPath());

		HdPrimvarsSchema primvars = HdPrimvarsSchema::GetFromParent(myPrim.dataSource);

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
			if (v.IsHolding<SdfAssetPath>()) {
				result.rpkPath = v.UncheckedGet<SdfAssetPath>();
			}
		}

		return result;
	}
};

class PrtProceduralPlugin : public HdGpGenerativeProceduralPlugin {
public:
	PrtProceduralPlugin() {
		prtContext = std::make_unique<PRTContext>();
		if (prtContext && prtContext->isAlive()) {
			// shortcut: we avoid creating an extra dll for the encoder
			prtx::ExtensionManager::instance().addFactory(HydraEncoderFactory::createInstance());
			LOG_INF << "Registered Hydra Encoder.";
		}
		else
			LOG_ERR << "Unable to load the ArcGIS Procedural Runtime PRT!";
	}

	~PrtProceduralPlugin() override {
		prtContext.reset();
	}

	HdGpGenerativeProcedural* Construct(const SdfPath& proceduralPrimPath) override {
		return new PrtProcedural(proceduralPrimPath);
	}
};

TF_REGISTRY_FUNCTION(TfType) {
	HdGpGenerativeProceduralPluginRegistry::Define<PrtProceduralPlugin,
	                                               HdGpGenerativeProceduralPlugin>();
}
