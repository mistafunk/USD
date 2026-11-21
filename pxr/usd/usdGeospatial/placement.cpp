//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/usd/usdGeospatial/binding.h"
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/usd/usdGeospatial/engineRegistry.h"
#include "pxr/usd/usdGeospatial/tokens.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/base/gf/math.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/tf/stringUtils.h"

#include <cmath>
#include <set>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// --- Ellipsoid geometry: closed form, independent of the engine. ---------

double
_EccentricitySquared(double inverseFlattening)
{
    const double f = inverseFlattening > 0.0 ? 1.0 / inverseFlattening : 0.0;
    return f * (2.0 - f);
}

GfVec3d
_ToEcef(double a, double e2, const GfVec3d& lonLatH)
{
    const double lon = GfDegreesToRadians(lonLatH[0]);
    const double lat = GfDegreesToRadians(lonLatH[1]);
    const double h = lonLatH[2];
    const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
    return GfVec3d((n + h) * std::cos(lat) * std::cos(lon),
                   (n + h) * std::cos(lat) * std::sin(lon),
                   (n * (1.0 - e2) + h) * std::sin(lat));
}

GfVec3d
_FromEcef(double a, double e2, const GfVec3d& ecef)
{
    const double lon = std::atan2(ecef[1], ecef[0]);
    const double p = std::hypot(ecef[0], ecef[1]);
    double lat = std::atan2(ecef[2], p * (1.0 - e2));
    double h = 0.0;
    for (int i = 0; i < 8; ++i) {
        const double n =
            a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
        h = p / std::cos(lat) - n;
        lat = std::atan2(ecef[2], p * (1.0 - e2 * n / (n + h)));
    }
    const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
    h = p / std::cos(lat) - n;
    return GfVec3d(GfRadiansToDegrees(lon), GfRadiansToDegrees(lat), h);
}

// East, north and up (ellipsoid normal) unit vectors in ECEF.
std::array<GfVec3d, 3>
_EnuBasis(double lonDeg, double latDeg)
{
    const double lon = GfDegreesToRadians(lonDeg);
    const double lat = GfDegreesToRadians(latDeg);
    const double sl = std::sin(lon), cl = std::cos(lon);
    const double sp = std::sin(lat), cp = std::cos(lat);
    return {GfVec3d(-sl, cl, 0.0),
            GfVec3d(-sp * cl, -sp * sl, cp),
            GfVec3d(cp * cl, cp * sl, sp)};
}

// --- Model attitude. ------------------------------------------------------

GfQuatd
_AxisRotation(const GfVec3d& axis, double degrees)
{
    const double half = GfDegreesToRadians(degrees) / 2.0;
    return GfQuatd(std::cos(half), axis * std::sin(half));
}

// The proposal's q_U(-heading) * q_E(pitch) * q_N(roll), acting on ENU
// vectors.
GfQuatd
_Attitude(const GfVec3d& headingPitchRoll)
{
    return _AxisRotation(GfVec3d(0, 0, 1), -headingPitchRoll[0]) *
           _AxisRotation(GfVec3d(1, 0, 0), headingPitchRoll[1]) *
           _AxisRotation(GfVec3d(0, 1, 0), headingPitchRoll[2]);
}

bool
_IsFinite(const GfVec3d& v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool
_HasDirections(const std::vector<TfToken>& axes,
               std::set<std::string> expected)
{
    std::set<std::string> got;
    for (const TfToken& axis : axes) {
        got.insert(axis.GetString());
    }
    return got == expected && axes.size() == expected.size();
}

std::string
_JoinOperations(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty() || a == b) return a;
    return a + "; then " + b;
}

} // anonymous namespace

bool
UsdGeospatialEvaluateOrientation(const UsdAttribute& attr, UsdTimeCode time,
                                 GfQuatd* result, std::string* error)
{
    *result = GfQuatd::GetIdentity();
    if (!attr || !attr.HasAuthoredValueOpinion()) {
        return true;
    }

    auto sample = [&](UsdTimeCode t, GfQuatd* q) {
        GfVec3d hpr;
        if (!attr.Get(&hpr, t)) {
            *error = "crs:orientation is blocked";
            return false;
        }
        if (!_IsFinite(hpr)) {
            *error = "crs:orientation is not finite";
            return false;
        }
        *q = _Attitude(hpr);
        return true;
    };

    double lower = 0.0, upper = 0.0;
    bool hasSamples = false;
    if (time.IsDefault() || attr.GetNumTimeSamples() == 0 ||
        !attr.GetBracketingTimeSamples(
            time.GetValue(), &lower, &upper, &hasSamples) ||
        !hasSamples || lower == upper) {
        return sample(hasSamples && !time.IsDefault()
                          ? UsdTimeCode(lower) : time, result);
    }

    GfQuatd q0, q1;
    if (!sample(UsdTimeCode(lower), &q0) || !sample(UsdTimeCode(upper), &q1)) {
        return false;
    }
    if (attr.GetStage()->GetInterpolationType() == UsdInterpolationTypeHeld) {
        *result = q0;
        return true;
    }
    double dot = q0.GetReal() * q1.GetReal() +
                 GfDot(q0.GetImaginary(), q1.GetImaginary());
    if (dot < 0.0) {
        q1 = -q1;
        dot = -dot;
    }
    if (dot < 1e-12) {
        *error = TfStringPrintf(
            "crs:orientation samples at %g and %g differ by 180 degrees; "
            "the interpolation path between them is not unique",
            lower, upper);
        return false;
    }
    *result = GfSlerp((time.GetValue() - lower) / (upper - lower), q0, q1);
    return true;
}

GfMatrix4d
UsdGeospatialComputeEnuToEcef(const UsdGeospatialCrsInfo& geographic,
                              const GfVec3d& lonLatH)
{
    const double e2 = _EccentricitySquared(geographic.inverseFlattening);
    const std::array<GfVec3d, 3> basis = _EnuBasis(lonLatH[0], lonLatH[1]);
    const GfVec3d origin = _ToEcef(geographic.semiMajorAxis, e2, lonLatH);
    GfMatrix4d m(1.0);
    for (int k = 0; k < 3; ++k) {
        m.SetRow(k, GfVec4d(basis[k][0], basis[k][1], basis[k][2], 0.0));
    }
    m.SetRow(3, GfVec4d(origin[0], origin[1], origin[2], 1.0));
    return m;
}

UsdGeospatialPlacementInputs
UsdGeospatialComputePlacementInputs(const UsdPrim& anchor, UsdTimeCode time)
{
    UsdGeospatialPlacementInputs inputs;
    inputs.anchorPath = anchor ? anchor.GetPath() : SdfPath();
    const char* path = inputs.anchorPath.GetText();

    if (!anchor || !anchor.HasAPI<UsdGeospatialCRSBindingAPI>()) {
        inputs.error = TfStringPrintf(
            "<%s> is not directly bound (GeospatialCRSBindingAPI)", path);
        return inputs;
    }
    const UsdGeospatialBinding binding = UsdGeospatialResolveBinding(anchor);
    if (!binding.error.empty()) {
        inputs.error = binding.error;
        return inputs;
    }
    inputs.wkt = binding.wkt.GetString();

    const UsdAttribute positionAttr =
        anchor.GetAttribute(UsdGeospatialTokens->crsPosition);
    if (!positionAttr || !positionAttr.HasAuthoredValueOpinion()) {
        inputs.error = TfStringPrintf("<%s> has no crs:position", path);
        return inputs;
    }
    if (!positionAttr.Get(&inputs.position, time)) {
        inputs.error = TfStringPrintf("<%s> crs:position is blocked", path);
        return inputs;
    }
    std::string orientationError;
    if (!UsdGeospatialEvaluateOrientation(
            anchor.GetAttribute(UsdGeospatialTokens->crsOrientation), time,
            &inputs.attitude, &orientationError)) {
        inputs.error =
            TfStringPrintf("<%s> %s", path, orientationError.c_str());
        return inputs;
    }

    const UsdStageWeakPtr stage = anchor.GetStage();
    inputs.metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
    inputs.upAxis = UsdGeomGetStageUpAxis(stage);

    GfMatrix4d local(1.0);
    bool resetsXformStack = false;
    UsdGeomXformable(anchor).GetLocalTransformation(
        &local, &resetsXformStack, time);
    inputs.hasOwnTransform = local != GfMatrix4d(1.0);
    return inputs;
}

// --- UsdGeospatialPlacement ------------------------------------------------

UsdGeospatialPlacement::UsdGeospatialPlacement()
    : _origin(0.0), _modelToFrame(1.0), _originEcef(0.0),
      _modelColumns{GfVec3d(0.0), GfVec3d(0.0), GfVec3d(0.0)},
      _originOutEcef(0.0),
      _outBasis{GfVec3d(0.0), GfVec3d(0.0), GfVec3d(0.0)}
{
}

bool
UsdGeospatialPlacement::_ToOutput(const std::vector<GfVec3d>& ecef,
                                  std::vector<GfVec3d>* output,
                                  std::string* error) const
{
    output->clear();
    output->reserve(ecef.size());
    for (const GfVec3d& p : ecef) {
        output->push_back(_FromEcef(_srcA, _srcE2, p));
    }
    // Through the output's geographic 3D CRS when there is a projection step
    // (see Resolve).
    const std::string& target =
        _outputGeographic.empty() ? _output : _outputGeographic;
    UsdGeospatialOperationResult result =
        _engineImpl->Transform(_sourceGeographic, target, _area, output);
    if (result && !_outputGeographic.empty()) {
        result = _engineImpl->Transform(_outputGeographic, _output, _area,
                                        output);
    }
    if (!result && error) {
        *error = result.error;
    }
    return bool(result);
}

bool
UsdGeospatialPlacement::ComputeOutputPoints(
    const std::vector<GfVec3d>& modelPoints,
    std::vector<GfVec3d>* outputPoints,
    std::string* error) const
{
    if (_status != Status::Ok) {
        if (error) {
            *error = _reason;
        }
        return false;
    }
    std::vector<GfVec3d> ecef;
    ecef.reserve(modelPoints.size());
    for (const GfVec3d& p : modelPoints) {
        ecef.push_back(_originEcef + _modelColumns[0] * p[0] +
                       _modelColumns[1] * p[1] + _modelColumns[2] * p[2]);
    }
    return _ToOutput(ecef, outputPoints, error);
}

bool
UsdGeospatialPlacement::ComputeFramePoints(
    const std::vector<GfVec3d>& modelPoints,
    std::vector<GfVec3d>* framePoints,
    std::string* error) const
{
    if (!ComputeOutputPoints(modelPoints, framePoints, error)) {
        return false;
    }
    for (GfVec3d& q : *framePoints) {
        if (_localFrame) {
            const GfVec3d d = _ToEcef(_outA, _outE2, q) - _originOutEcef;
            q = GfVec3d(GfDot(d, _outBasis[0]), GfDot(d, _outBasis[1]),
                        GfDot(d, _outBasis[2]));
        } else {
            q -= _origin;
        }
    }
    return true;
}

// --- UsdGeospatialPlacementQuery -------------------------------------------

UsdGeospatialPlacementQuery::UsdGeospatialPlacementQuery(
    const std::string& output, const std::string& engineName)
    : _output(output)
{
    _engine = UsdGeospatialEngineRegistry::GetInstance().GetEngine(engineName);
    if (!_engine) {
        _error = engineName.empty()
            ? "no coordinate operation engine is available"
            : "no coordinate operation engine '" + engineName + "'";
        return;
    }
    _outputInfo = _engine->DescribeCrs(output);
    if (_outputInfo.valid && _outputInfo.dimension == 2 &&
        !_outputInfo.promoted3D.empty()) {
        // The consumer asks for heights too: ellipsoidal ones.
        _output = _outputInfo.promoted3D;
        _outputInfo = _engine->DescribeCrs(_output);
    }
    if (!_outputInfo.valid) {
        _error = "output CRS: " + _outputInfo.error;
    } else if (_outputInfo.dimension != 3) {
        _error = "output CRS '" + _outputInfo.name + "' is not 3D";
    }
}

UsdGeospatialPlacement
UsdGeospatialPlacementQuery::Resolve(const UsdPrim& anchor,
                                     UsdTimeCode time) const
{
    return Resolve(UsdGeospatialComputePlacementInputs(anchor, time));
}

UsdGeospatialPlacement
UsdGeospatialPlacementQuery::Resolve(
    const UsdGeospatialPlacementInputs& inputs) const
{
    using Status = UsdGeospatialPlacement::Status;
    UsdGeospatialPlacement r;
    r._anchorPath = inputs.anchorPath;
    r._engine = _engine ? _engine->GetName() : std::string();
    const char* path = r._anchorPath.GetText();

    auto fail = [&r](Status status, const std::string& reason) {
        r._status = status;
        r._reason = reason;
        return r;
    };

    if (!IsValid()) {
        return fail(Status::Error, _error);
    }
    if (!inputs.error.empty()) {
        return fail(Status::Error, inputs.error);
    }
    if (inputs.wkt.empty()) {
        return fail(Status::Error, TfStringPrintf(
            "<%s> direct binding without a CRS definition (crs:wkt)", path));
    }
    const GfVec3d& position = inputs.position;
    if (!_IsFinite(position)) {
        return fail(Status::Error,
                    TfStringPrintf("<%s> crs:position is not finite", path));
    }
    const GfQuatd& attitude = inputs.attitude;

    // The anchor's own ordinary transform is a post-placement adjustment in
    // the working chart; that convention is still a candidate in the
    // proposal and not implemented yet.
    if (inputs.hasOwnTransform) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> has its own ordinary transform; post-placement adjustment "
            "of an anchor is not supported yet", path));
    }

    // The source CRS and the anchor's geodetic position.
    const std::string& wkt = inputs.wkt;
    const UsdGeospatialCrsInfo source = _engine->DescribeCrs(wkt);
    if (!source.valid) {
        return fail(Status::Error, TfStringPrintf(
            "<%s> crs:wkt cannot be used: %s", path, source.error.c_str()));
    }
    if (source.dimension != 3) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> CRS '%s' is 2D: its height would come from the EGM2008 "
            "default vertical reference, which is not supported yet",
            path, source.name.c_str()));
    }
    r._sourceCrs = source.name;
    const bool axesOk = source.isGeocentric
        ? _HasDirections(source.axisDirections,
                         {"geocentricX", "geocentricY", "geocentricZ"})
        : _HasDirections(source.axisDirections, {"east", "north", "up"});
    if (!axesOk) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> CRS '%s' has axis directions (%s) outside the supported "
            "profile (east/north/up, geocentric X/Y/Z)", path,
            source.name.c_str(),
            TfStringJoin(TfToStringVector(source.axisDirections),
                         ", ").c_str()));
    }
    if (source.isGeocentric && position[0] == 0.0 && position[1] == 0.0) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> geocentric position on the polar axis does not determine "
            "the reference meridian for the model attitude", path));
    }

    std::vector<GfVec3d> geo{position};
    const UsdGeospatialOperationResult toGeographic =
        _engine->Transform(wkt, source.geographic3D,
                           UsdGeospatialAreaOfInterest(), &geo);
    if (!toGeographic) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> position cannot be converted to geographic 3D: %s",
            path, toGeographic.error.c_str()));
    }
    // Operations into the output CRS are chosen for the anchor's location:
    // many datum transformations are regional.
    const UsdGeospatialAreaOfInterest area =
        UsdGeospatialAreaOfInterest::AtPoint(geo[0][0], geo[0][1]);
    // Into a projected output, go through its geographic 3D CRS: datum
    // shift first, then the projection as a conversion. Direct operations
    // into a projected CRS with an ellipsoidal height axis keep the source's
    // height through the datum shift (PROJ wraps it in push/pop v_3), which
    // loses the height change of e.g. a Helmert shift (2 m from WGS 72,
    // 21 m from NAD27 at Redlands).
    const bool viaGeographic = !_outputInfo.isGeographic &&
        !_outputInfo.isGeocentric && !_outputInfo.geographic3D.empty() &&
        _outputInfo.geographic3D != _output;
    const std::string& datumTarget =
        viaGeographic ? _outputInfo.geographic3D : _output;
    UsdGeospatialOperationResult toOutput =
        _engine->CanTransform(source.geographic3D, datumTarget, area);
    if (toOutput && viaGeographic) {
        const UsdGeospatialOperationResult toProjection =
            _engine->CanTransform(datumTarget, _output, area);
        toOutput.succeeded = toProjection.succeeded;
        toOutput.error = toProjection.error;
        toOutput.operation =
            _JoinOperations(toOutput.operation, toProjection.operation);
        toOutput.accuracy =
            (toOutput.accuracy < 0.0 || toProjection.accuracy < 0.0)
            ? -1.0 : toOutput.accuracy + toProjection.accuracy;
    }
    if (!toOutput) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> no coordinate operation from '%s' to the output CRS '%s': "
            "%s", path, source.name.c_str(), _outputInfo.name.c_str(),
            toOutput.error.c_str()));
    }

    r._operation = _JoinOperations(toGeographic.operation, toOutput.operation);
    r._skippedOperations = toGeographic.skippedOperations;
    r._skippedOperations.insert(r._skippedOperations.end(),
                                toOutput.skippedOperations.begin(),
                                toOutput.skippedOperations.end());
    r._accuracy = (toGeographic.accuracy < 0.0 || toOutput.accuracy < 0.0)
        ? -1.0 : toGeographic.accuracy + toOutput.accuracy;
    r._engineImpl = _engine;
    r._area = area;
    r._sourceGeographic = source.geographic3D;
    r._output = _output;
    r._outputGeographic = viaGeographic ? datumTarget : std::string();
    r._srcA = source.semiMajorAxis;
    r._srcE2 = _EccentricitySquared(source.inverseFlattening);

    // Model point map: stage units -> model ENU metres -> attitude ->
    // source ECEF.
    const double metersPerUnit = inputs.metersPerUnit;
    const bool yUp = inputs.upAxis == UsdGeomTokens->y;
    // Stage (x, y, z) to model (E, N, U): Z-up identity, Y-up (x, -z, y).
    const std::array<GfVec3d, 3> axisMap = yUp
        ? std::array<GfVec3d, 3>{GfVec3d(1, 0, 0), GfVec3d(0, 0, 1),
                                 GfVec3d(0, -1, 0)}
        : std::array<GfVec3d, 3>{GfVec3d(1, 0, 0), GfVec3d(0, 1, 0),
                                 GfVec3d(0, 0, 1)};
    const std::array<GfVec3d, 3> basis = _EnuBasis(geo[0][0], geo[0][1]);
    r._originEcef = _ToEcef(r._srcA, r._srcE2, geo[0]);
    for (int k = 0; k < 3; ++k) {
        const GfVec3d v = attitude.Transform(axisMap[k]);
        r._modelColumns[k] =
            metersPerUnit * (basis[0] * v[0] + basis[1] * v[1] +
                             basis[2] * v[2]);
    }

    // Origin and frame in the output CRS.
    r._status = Status::Ok;
    std::vector<GfVec3d> origin;
    std::string error;
    if (!r._ToOutput({r._originEcef}, &origin, &error)) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> coordinate operation failed: %s", path, error.c_str()));
    }
    r._origin = origin[0];
    if (_outputInfo.isGeographic) {
        r._localFrame = true;
        r._outA = _outputInfo.semiMajorAxis;
        r._outE2 = _EccentricitySquared(_outputInfo.inverseFlattening);
        r._originOutEcef = _ToEcef(r._outA, r._outE2, r._origin);
        r._outBasis = _EnuBasis(r._origin[0], r._origin[1]);
    }

    // Linearization by central differences, one metre either side.
    const double step = metersPerUnit > 0.0 ? 1.0 / metersPerUnit : 1.0;
    std::vector<GfVec3d> probes;
    for (int k = 0; k < 3; ++k) {
        GfVec3d e(0.0);
        e[k] = step;
        probes.push_back(e);
        probes.push_back(-e);
    }
    std::vector<GfVec3d> frame;
    if (!r.ComputeFramePoints(probes, &frame, &error)) {
        return fail(Status::Unsupported, TfStringPrintf(
            "<%s> coordinate operation failed: %s", path, error.c_str()));
    }
    r._modelToFrame.SetIdentity();
    for (int k = 0; k < 3; ++k) {
        // Row-vector convention: row k is the image of model axis k.
        const GfVec3d column = (frame[2 * k] - frame[2 * k + 1]) / (2 * step);
        r._modelToFrame.SetRow(k, GfVec4d(column[0], column[1], column[2],
                                          0.0));
    }
    return r;
}

PXR_NAMESPACE_CLOSE_SCOPE
