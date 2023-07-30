/**
 * Serlio - Esri CityEngine Plugin for Autodesk Maya
 *
 * See https://github.com/esri/serlio for build and usage instructions.
 *
 * Copyright (c) 2012-2019 Esri R&D Center Zurich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "prtContext.h"

#include "prt/LogLevel.h"

#include "pxr/base/tf/diagnostic.h"

#include <mutex>

namespace {

constexpr bool DBG = false;

constexpr const wchar_t* PRT_EXT_SUBDIR = L"prt_ext";
constexpr prt::LogLevel PRT_LOG_LEVEL = prt::LOG_DEBUG;
constexpr bool ENABLE_LOG_CONSOLE = true;
constexpr bool ENABLE_LOG_FILE = false;

} // namespace

PRTContext::PRTContext(const std::vector<std::wstring>& addExtDirs) : mPluginRootPath(prtu::getPluginRoot()) {
	if (DBG)
		LOG_DBG << "Initializing PRT context, plugin root path is " << mPluginRootPath.wstring();

	std::vector<std::wstring> extensionPaths = {(mPluginRootPath / PRT_EXT_SUBDIR).wstring()};
	extensionPaths.insert(extensionPaths.end(), addExtDirs.begin(), addExtDirs.end());
	if (DBG)
		LOG_DBG << "looking for prt extensions at\n" << extensionPaths;

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	const auto extensionPathPtrs = prtu::toPtrVec(extensionPaths);
	mPRTHandle.reset(prt::init(extensionPathPtrs.data(), extensionPathPtrs.size(), PRT_LOG_LEVEL, &status));

	if (!mPRTHandle || status != prt::STATUS_OK) {
		LOG_FTL << "Could not initialize PRT: " << prt::getStatusDescription(status);
		mPRTHandle.reset();
	}
	else {
		mPRTCache.reset(prt::CacheObject::create(prt::CacheObject::CACHE_TYPE_DEFAULT));
		// mResolveMapCache = std::make_unique<ResolveMapCache>();
	}

	using namespace pxr;
	TF_STATUS("Initialized PRT %s", prt::getVersion()->mVersion);
}

bool PRTContext::isAlive() const {
	return static_cast<bool>(mPRTHandle);
}

void PRTContext::registerClient() {
	std::lock_guard<std::mutex> lock(mClientMutex);
	mClientCount++;

	if (ENABLE_LOG_CONSOLE && !mLogHandler) {
		mLogHandler = std::make_unique<logging::LogHandler>();
		prt::addLogHandler(mLogHandler.get());
	}

	//	if (ENABLE_LOG_FILE) {
	//		const std::wstring logPath = (mPluginRootPath / L"serlio.log").wstring();
	//		mFileLogHandler = prt::FileLogHandler::create(prt::LogHandler::ALL,
	//		                                              prt::LogHandler::ALL_COUNT, logPath.c_str());
	//		prt::addLogHandler(mFileLogHandler);
	//	}
}

void PRTContext::unregisterClient() {
	std::lock_guard<std::mutex> lock(mClientMutex);
	mClientCount--;

	if (ENABLE_LOG_CONSOLE && mClientCount == 0) {
		if (mLogHandler) {
			prt::removeLogHandler(mLogHandler.get());
			mLogHandler.reset(); // bit of a workaround to prevent boost log related hangers at e.g. usdview exit
		}
	}

	if (ENABLE_LOG_FILE && (mFileLogHandler != nullptr)) {
		prt::removeLogHandler(mFileLogHandler);
		mFileLogHandler->destroy();
		mFileLogHandler = nullptr;
	}
}

PRTContext::~PRTContext() {
	// the cache needs to be destructed before PRT
	mPRTCache.reset();
	mPRTHandle.reset();
}
