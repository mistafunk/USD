#include "prtCallbacks.h"
#include "prtContext.h"
#include "prtHydraEncoder.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/arch/stackTrace.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/fileUtils.h"

#include "pxr/imaging/hdGp/generativeProceduralPlugin.h"
#include "pxr/imaging/hdGp/generativeProceduralPluginRegistry.h"

#include "pxr/imaging/hd/materialSchema.h"
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
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

constexpr bool DBG = false;

constexpr const wchar_t* ENC_ID_ATTR_EVAL = L"com.esri.prt.core.AttributeEvalEncoder";
constexpr const wchar_t* ENC_ID_CGA_ERROR = L"com.esri.prt.core.CGAErrorEncoder";
constexpr const wchar_t* ENC_ID_CGA_PRINT = L"com.esri.prt.core.CGAPrintEncoder";

} // namespace

TF_DEFINE_PRIVATE_TOKENS(prtTokens, (sourceMeshPath)(rpkPath));

class PrtProcedural : public HdGpGenerativeProcedural {
public:
	PrtProcedural(PRTContext& prtContext, const SdfPath& proceduralPrimPath)
	    : HdGpGenerativeProcedural(proceduralPrimPath), mPRTContext(prtContext) {
		if (DBG)
			TF_STATUS("PrtProcedural c'tor");
		mPRTContext.registerClient();
	}

	virtual ~PrtProcedural() override {
		mPRTContext.unregisterClient();
		if (DBG)
			TF_STATUS("PrtProcedural d'tor");
	}

	DependencyMap UpdateDependencies(const HdSceneIndexBaseRefPtr& inputScene) override {
		DependencyMap result;
		_Args args = _GetArgs(inputScene);
		if (!args.sourceMeshPath.IsEmpty()) {
			result[args.sourceMeshPath] = HdPrimvarsSchema::GetDefaultLocator();
		}
		return result;
	}

	ChildPrimTypeMap Update(const HdSceneIndexBaseRefPtr& inputScene, const ChildPrimTypeMap& previousResult,
	                        const DependencyMap& dirtiedDependencies,
	                        HdSceneIndexObserver::DirtiedPrimEntries* outputDirtiedPrims) override {
		ChildPrimTypeMap result;

		_Args args = _GetArgs(inputScene);
		LOG_DBG << "source mesh: " << args.sourceMeshPath.GetText() << "\n   rpk: "
		        << args.rpkPath.GetResolvedPath().c_str();

		if (args.sourceMeshPath.IsEmpty()) {
			mGeneratedData.clear();
			return result;
		}

		HdSceneIndexPrim sourceMeshPrim = inputScene->GetPrim(args.sourceMeshPath);
		if (sourceMeshPrim.primType != HdPrimTypeTokens->mesh) {
			mGeneratedData.clear();
			return result;
		}

		LOG_DBG << "source mesh path: " << args.sourceMeshPath.GetText();
		HdMeshSchema sourceMeshSchema = HdMeshSchema::GetFromParent(sourceMeshPrim.dataSource);
		if (!sourceMeshSchema) {
			LOG_WRN << "cannot get mesh schema from " << args.sourceMeshPath.GetText();
			mGeneratedData.clear();
			return result;
		}
		TfToken sourcePrimName = args.sourceMeshPath.GetNameToken();

		HdPrimvarsSchema primvarsSchema = HdPrimvarsSchema::GetFromParent(sourceMeshPrim.dataSource);
		HdPrimvarSchema pointsPrimvarSchema = primvarsSchema.GetPrimvar(HdPrimvarsSchemaTokens->points);
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

		auto xformSchema = HdXformSchema::GetFromParent(sourceMeshPrim.dataSource);
		const auto matrixDs = xformSchema.GetMatrix();
		const GfMatrix4d xformMatrix = matrixDs->GetTypedValue(0.0);

		std::vector<double> prtPoints;
		prtPoints.reserve(points.size() * 3);
		for (const GfVec3f& v : points) {
			GfVec3f vw = xformMatrix.Transform(v);
			prtPoints.push_back(vw[0]);
			prtPoints.push_back(vw[1]);
			prtPoints.push_back(vw[2]);
		}

		InitialShapeBuilderUPtr isb(prt::InitialShapeBuilder::create());
		const prt::Status setGeoStatus =
		        isb->setGeometry(prtPoints.data(), prtPoints.size(), (const uint32_t*)faceVertexIndices.cdata(),
		                         faceVertexIndices.size(), (const uint32_t*)faceCounts.cdata(), faceCounts.size());
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

			// workaround for PRT limitation: PRT does not recursively create a resolvemap from RPK
			// within USDZ
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

		if (DBG) LOG_DBG << prtu::objectToXML(resolveMap.get());

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
		for (const auto& primvarName : primvars.GetPrimvarNames()) {
			HdPrimvarSchema primvarSchema = primvars.GetPrimvar(primvarName);
			HdSampledDataSourceHandle dataSourceHandle = primvarSchema.GetPrimvarValue();
			const std::wstring u16Key = prtu::toUTF16FromUTF8(primvarName.GetString());
			VtValue value = dataSourceHandle->GetValue(0);
			LOG_DBG << "attr: " << primvarName.GetString() << ", type: " << value.GetTypeName();
			if (value.IsHolding<float>()) {
				LOG_DBG << "    value: " << value.UncheckedGet<float>();
				amb->setFloat(u16Key.c_str(), value.UncheckedGet<float>());
			}
			else if (value.IsHolding<std::string>()) {
				LOG_DBG << "    value: " << value.UncheckedGet<std::string>();
				amb->setString(u16Key.c_str(), prtu::toUTF16FromUTF8(value.UncheckedGet<std::string>()).c_str());
			}
			else if (value.IsHolding<bool>()) {
				LOG_DBG << "    value: " << value.UncheckedGet<bool>();
				amb->setBool(u16Key.c_str(), value.UncheckedGet<bool>());
			}
		}
		AttributeMapUPtr initialShapeAttributes(amb->createAttributeMapAndReset());
		isb->setAttributes(ruleFileKey.c_str(), startRule.c_str(), 0, L"sourceMesh", initialShapeAttributes.get(),
		                   resolveMap.get());

		InitialShapeUPtr initialShape(isb->createInitialShapeAndReset());

		AttributeMapUPtr hydraEncOpts = prtu::createValidatedOptions(ENC_ID_HYDRA);
		const std::vector<const wchar_t*> encIDs = {ENC_ID_HYDRA};
		const AttributeMapNOPtrVector encOpts = {hydraEncOpts.get()};
		assert(encIDs.size() == encOpts.size());

		SdfPath prtPrimPath = _GetProceduralPrimPath();
		SdfPath prtChildPath = prtPrimPath.AppendChild(sourcePrimName);

		std::unique_ptr<PrtCallbacks> outputHandler(new PrtCallbacks(prtChildPath, result, mGeneratedData, amb));
		InitialShapeNOPtrVector initialShapes = {initialShape.get()};
		const prt::Status generateStatus =
		        prt::generate(initialShapes.data(), initialShapes.size(), nullptr, encIDs.data(), encIDs.size(),
		                      encOpts.data(), outputHandler.get(), mPRTContext.mPRTCache.get(), nullptr);
		if (generateStatus != prt::STATUS_OK) {
			LOG_ERR << "PRT generate failed with status = " << prt::getStatusDescription(generateStatus);
			return result;
		}

		if (outputDirtiedPrims != nullptr) {
			outputDirtiedPrims->emplace_back(prtChildPath, HdMeshSchema::GetDefaultLocator());
		}

		return result;
	}

	// called concurrently from multiple threads
	HdSceneIndexPrim GetChildPrim(const HdSceneIndexBaseRefPtr& inputScene, const SdfPath& childPrimPath) override {
		LOG_DBG << "GetChildPrim: " << childPrimPath.GetText();
		HdSceneIndexPrim result;
		auto it = mGeneratedData.find(childPrimPath);
		if (it != mGeneratedData.end()) {
			result.primType = it->second.second;
			result.dataSource = it->second.first;
		}
		return result;
	}

private:
	PRTContext& mPRTContext;
	GeneratedData mGeneratedData;

	struct _Args {
		_Args() = default;

		SdfPath sourceMeshPath;
		SdfAssetPath rpkPath;
	};

	_Args _GetArgs(const HdSceneIndexBaseRefPtr& inputScene) {
		_Args result;

		HdSceneIndexPrim myPrim = inputScene->GetPrim(_GetProceduralPrimPath());

		HdPrimvarsSchema primvars = HdPrimvarsSchema::GetFromParent(myPrim.dataSource);

		if (HdSampledDataSourceHandle sourceMeshDs = primvars.GetPrimvar(prtTokens->sourceMeshPath).GetPrimvarValue()) {
			VtValue v = sourceMeshDs->GetValue(0.0f);

			if (v.IsHolding<VtArray<SdfPath>>()) {
				VtArray<SdfPath> a = v.UncheckedGet<VtArray<SdfPath>>();
				if (a.size() == 1) {
					result.sourceMeshPath = a[0];
				}
			}
		}

		if (HdSampledDataSourceHandle ds = primvars.GetPrimvar(prtTokens->rpkPath).GetPrimvarValue()) {
			VtValue v = ds->GetValue(0.0f);
			if (v.IsHolding<SdfAssetPath>()) {
				result.rpkPath = v.UncheckedGet<SdfAssetPath>();
			}
		}

		return result;
	}
};

namespace {

std::once_flag prtContextInitializationFlag;
PRTContextUPtr prtContext;

} // namespace

class PrtProceduralPlugin : public HdGpGenerativeProceduralPlugin {
public:
	PrtProceduralPlugin() {
		if (DBG)
			TF_STATUS("PrtProceduralPlugin c'tor");
		std::call_once(prtContextInitializationFlag, []() {
			if (prtContext)
				TF_CODING_ERROR("Unexpected state of PRT context!");
			prtContext = std::make_unique<PRTContext>();
			if (prtContext && prtContext->isAlive()) {
				// shortcut: we avoid creating an extra dll for the encoder
				prtx::ExtensionManager::instance().addFactory(HydraEncoderFactory::createInstance());
				if (DBG)
					TF_STATUS("Registered Hydra Encoder.");
			}
			else
				TF_FATAL_ERROR("Unable to load the ArcGIS Procedural Runtime PRT!");
		});
	}

	virtual ~PrtProceduralPlugin() override {
		if (DBG)
			TF_STATUS("PrtProceduralPlugin d'tor"); // this is not called by default
	}

	HdGpGenerativeProcedural* Construct(const SdfPath& proceduralPrimPath) override {
		return new PrtProcedural(*prtContext, proceduralPrimPath);
	}
};

TF_REGISTRY_FUNCTION(TfType) {
	HdGpGenerativeProceduralPluginRegistry::Define<PrtProceduralPlugin, HdGpGenerativeProceduralPlugin>();
}
