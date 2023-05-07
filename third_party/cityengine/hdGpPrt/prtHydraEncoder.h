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

#pragma once

#include "prtx/EncodePreparator.h"
#include "prtx/Encoder.h"
#include "prtx/EncoderFactory.h"
#include "prtx/EncoderInfoBuilder.h"
#include "prtx/PRTUtils.h"
#include "prtx/ResolveMap.h"
#include "prtx/Singleton.h"

#include "prt/ContentType.h"
#include "prt/InitialShape.h"

#include <iostream>
#include <stdexcept>
#include <string>

constexpr const wchar_t* ENC_ID_HYDRA = L"HydraEncoder";

class PrtCallbacks;

class HydraEncoder : public prtx::GeometryEncoder {
public:
	HydraEncoder(const std::wstring& id, const prt::AttributeMap* options, prt::Callbacks* callbacks);
	~HydraEncoder() override = default;

public:
	void init(prtx::GenerateContext& context) override;
	void encode(prtx::GenerateContext& context, size_t initialShapeIndex) override;
	void finish(prtx::GenerateContext& context) override;

private:
	void convertGeometry(const prtx::InitialShape& initialShape,
	                     const prtx::EncodePreparator::InstanceVector& instances, PrtCallbacks* callbacks,
	                     prt::Cache* cache);
};

class HydraEncoderFactory : public prtx::EncoderFactory, public prtx::Singleton<HydraEncoderFactory> {
public:
	static HydraEncoderFactory* createInstance();

	explicit HydraEncoderFactory(const prt::EncoderInfo* info) : prtx::EncoderFactory(info) {}
	~HydraEncoderFactory() override = default;

	HydraEncoder* create(const prt::AttributeMap* options, prt::Callbacks* callbacks) const override {
		return new HydraEncoder(getID(), options, callbacks);
	}
};
