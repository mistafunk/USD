//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/engine.h"

#include "pxr/base/tf/stringUtils.h"

#include <proj.h>

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Owning handles for PROJ objects.
struct _PjDeleter {
    void operator()(PJ* p) const { proj_destroy(p); }
};
using _PjPtr = std::unique_ptr<PJ, _PjDeleter>;

struct _ListDeleter {
    void operator()(PJ_OBJ_LIST* l) const { proj_list_destroy(l); }
};
using _ListPtr = std::unique_ptr<PJ_OBJ_LIST, _ListDeleter>;

struct _FactoryDeleter {
    void operator()(PJ_OPERATION_FACTORY_CONTEXT* f) const {
        proj_operation_factory_context_destroy(f);
    }
};
using _FactoryPtr =
    std::unique_ptr<PJ_OPERATION_FACTORY_CONTEXT, _FactoryDeleter>;

} // anonymous namespace

/// \class UsdGeospatialProjEngine
///
/// Coordinate operation engine backed by PROJ. Operations are chosen by
/// proj_create_operations for the area of interest, with ballpark
/// operations disabled and operations that need missing grids discarded, so
/// a CRS pair without a real operation is reported as unsupported rather
/// than approximated.
///
class UsdGeospatialProjEngine : public UsdGeospatialCoordinateOperationEngine
{
public:
    UsdGeospatialProjEngine();
    ~UsdGeospatialProjEngine() override;

    std::string GetName() const override { return "proj"; }

    UsdGeospatialCrsInfo DescribeCrs(const std::string& definition) override;

    UsdGeospatialOperationResult CanTransform(
        const std::string& source, const std::string& target,
        const UsdGeospatialAreaOfInterest& area) override;

    UsdGeospatialOperationResult Transform(
        const std::string& source, const std::string& target,
        const UsdGeospatialAreaOfInterest& area,
        std::vector<GfVec3d>* points) override;

private:
    struct _Operation {
        _PjPtr pj;
        std::string description;
        double accuracy = -1.0;
        std::string error;
        std::vector<std::string> skipped;
    };

    std::string _LastError() const;
    _PjPtr _Create(const std::string& definition, std::string* error) const;
    void _AppendAxes(const PJ* crs, std::vector<TfToken>* axes) const;
    _PjPtr _Geographic3D(const PJ* crs, std::string* error) const;
    const _Operation& _GetOperation(const std::string& source,
                                    const std::string& target,
                                    const UsdGeospatialAreaOfInterest& area);
    _FactoryPtr _NewFactory(const UsdGeospatialAreaOfInterest& area,
                            PROJ_GRID_AVAILABILITY_USE gridUse) const;
    UsdGeospatialOperationResult _Result(const _Operation& op) const;
    double _Accuracy(const PJ* op) const;
    std::vector<std::string> _SkippedForMissingGrids(
        const PJ* src, const PJ* dst, const UsdGeospatialAreaOfInterest& area,
        double usedAccuracy) const;

    // PROJ contexts are not thread safe; one context, one lock.
    mutable std::mutex _mutex;
    PJ_CONTEXT* _ctx;
    // Source, target and area of interest (rounded) to the operation.
    std::map<std::tuple<std::string, std::string, std::string>, _Operation>
        _operations;
};

USDGEOSPATIAL_DEFINE_ENGINE(UsdGeospatialProjEngine);

UsdGeospatialProjEngine::UsdGeospatialProjEngine()
    : _ctx(proj_context_create())
{
    // Errors are reported in results, not printed.
    proj_log_level(_ctx, PJ_LOG_NONE);
}

UsdGeospatialProjEngine::~UsdGeospatialProjEngine()
{
    _operations.clear();
    proj_context_destroy(_ctx);
}

std::string
UsdGeospatialProjEngine::_LastError() const
{
    const int err = proj_context_errno(_ctx);
    const char* text = err ? proj_context_errno_string(_ctx, err) : nullptr;
    return text ? std::string(text) : std::string("unknown PROJ error");
}

_PjPtr
UsdGeospatialProjEngine::_Create(const std::string& definition,
                                 std::string* error) const
{
    _PjPtr crs(proj_create(_ctx, definition.c_str()));
    if (!crs) {
        *error = "PROJ cannot parse the CRS definition: " + _LastError();
    } else if (!proj_is_crs(crs.get())) {
        *error = "the definition is not a CRS";
        crs.reset();
    }
    return crs;
}

void
UsdGeospatialProjEngine::_AppendAxes(const PJ* crs,
                                     std::vector<TfToken>* axes) const
{
    if (proj_get_type(crs) == PJ_TYPE_COMPOUND_CRS) {
        for (int i = 0; i < 2; ++i) {
            if (_PjPtr sub{proj_crs_get_sub_crs(_ctx, crs, i)}) {
                _AppendAxes(sub.get(), axes);
            }
        }
        return;
    }
    if (proj_get_type(crs) == PJ_TYPE_BOUND_CRS) {
        if (_PjPtr base{proj_get_source_crs(_ctx, crs)}) {
            _AppendAxes(base.get(), axes);
        }
        return;
    }
    _PjPtr cs(proj_crs_get_coordinate_system(_ctx, crs));
    if (!cs) {
        return;
    }
    const int count = proj_cs_get_axis_count(_ctx, cs.get());
    for (int i = 0; i < count; ++i) {
        const char* direction = nullptr;
        if (proj_cs_get_axis_info(_ctx, cs.get(), i, nullptr, nullptr,
                                  &direction, nullptr, nullptr, nullptr,
                                  nullptr) && direction) {
            axes->push_back(TfToken(direction));
        }
    }
}

// The datum's geographic 3D CRS (longitude, latitude, ellipsoidal height).
_PjPtr
UsdGeospatialProjEngine::_Geographic3D(const PJ* crs,
                                       std::string* error) const
{
    _PjPtr geodetic(proj_crs_get_geodetic_crs(_ctx, crs));
    if (!geodetic) {
        *error = "the CRS has no geodetic datum: " + _LastError();
        return nullptr;
    }
    const PJ_TYPE type = proj_get_type(geodetic.get());
    if (type == PJ_TYPE_GEOGRAPHIC_2D_CRS ||
        type == PJ_TYPE_GEOGRAPHIC_3D_CRS) {
        _PjPtr geo3d(proj_crs_promote_to_3D(_ctx, nullptr, geodetic.get()));
        if (!geo3d) {
            *error = "cannot make a 3D geographic CRS: " + _LastError();
        }
        return geo3d;
    }

    // A geocentric CRS: build the geographic CRS of the same datum.
    _PjPtr datum(proj_crs_get_datum_ensemble(_ctx, geodetic.get()));
    if (!datum) {
        datum.reset(proj_crs_get_datum(_ctx, geodetic.get()));
    }
    _PjPtr cs(proj_create_ellipsoidal_3D_cs(
        _ctx, PJ_ELLPS3D_LONGITUDE_LATITUDE_HEIGHT, nullptr, 0.0, nullptr,
        0.0));
    if (!datum || !cs) {
        *error = "cannot build the datum's geographic CRS: " + _LastError();
        return nullptr;
    }
    _PjPtr geo3d(proj_create_geographic_crs_from_datum(
        _ctx, proj_get_name(geodetic.get()), datum.get(), cs.get()));
    if (!geo3d) {
        *error = "cannot build the datum's geographic CRS: " + _LastError();
    }
    return geo3d;
}

UsdGeospatialCrsInfo
UsdGeospatialProjEngine::DescribeCrs(const std::string& definition)
{
    std::lock_guard<std::mutex> lock(_mutex);
    UsdGeospatialCrsInfo info;

    _PjPtr crs = _Create(definition, &info.error);
    if (!crs) {
        return info;
    }
    const char* name = proj_get_name(crs.get());
    info.name = name ? name : "";
    _AppendAxes(crs.get(), &info.axisDirections);
    info.dimension = static_cast<int>(info.axisDirections.size());

    const PJ_TYPE type = proj_get_type(crs.get());
    info.isGeographic = type == PJ_TYPE_GEOGRAPHIC_2D_CRS ||
                        type == PJ_TYPE_GEOGRAPHIC_3D_CRS;
    info.isGeocentric = !info.axisDirections.empty() &&
                        info.axisDirections[0] == "geocentricX";

    _PjPtr ellipsoid(proj_get_ellipsoid(_ctx, crs.get()));
    double semiMinor = 0.0;
    int computed = 0;
    if (!ellipsoid ||
        !proj_ellipsoid_get_parameters(_ctx, ellipsoid.get(),
                                       &info.semiMajorAxis, &semiMinor,
                                       &computed, &info.inverseFlattening)) {
        info.error = "the CRS has no ellipsoid: " + _LastError();
        return info;
    }

    _PjPtr geo3d = _Geographic3D(crs.get(), &info.error);
    if (!geo3d) {
        return info;
    }
    const char* wkt = proj_as_wkt(_ctx, geo3d.get(), PJ_WKT2_2019, nullptr);
    if (!wkt) {
        info.error = "cannot write the geographic CRS: " + _LastError();
        return info;
    }
    info.geographic3D = wkt;

    if (info.dimension == 2 &&
        (type == PJ_TYPE_GEOGRAPHIC_2D_CRS || type == PJ_TYPE_PROJECTED_CRS)) {
        _PjPtr promoted(proj_crs_promote_to_3D(_ctx, nullptr, crs.get()));
        const char* promotedWkt = promoted
            ? proj_as_wkt(_ctx, promoted.get(), PJ_WKT2_2019, nullptr)
            : nullptr;
        if (promotedWkt) {
            info.promoted3D = promotedWkt;
        }
    }
    info.valid = true;
    return info;
}

_FactoryPtr
UsdGeospatialProjEngine::_NewFactory(const UsdGeospatialAreaOfInterest& area,
                                     PROJ_GRID_AVAILABILITY_USE gridUse) const
{
    _FactoryPtr factory(proj_create_operation_factory_context(_ctx, nullptr));
    proj_operation_factory_context_set_allow_ballpark_transformations(
        _ctx, factory.get(), 0);
    proj_operation_factory_context_set_grid_availability_use(
        _ctx, factory.get(), gridUse);
    proj_operation_factory_context_set_spatial_criterion(
        _ctx, factory.get(), PROJ_SPATIAL_CRITERION_PARTIAL_INTERSECTION);
    proj_operation_factory_context_set_discard_superseded(
        _ctx, factory.get(), 1);
    if (!area.IsEmpty()) {
        proj_operation_factory_context_set_area_of_interest(
            _ctx, factory.get(), area.west, area.south, area.east, area.north);
    }
    return factory;
}

const UsdGeospatialProjEngine::_Operation&
UsdGeospatialProjEngine::_GetOperation(const std::string& source,
                                       const std::string& target,
                                       const UsdGeospatialAreaOfInterest& area)
{
    const auto key = std::make_tuple(
        source, target, area.IsEmpty() ? std::string() : TfStringPrintf(
            "%.6f %.6f %.6f %.6f", area.west, area.south, area.east,
            area.north));
    auto it = _operations.find(key);
    if (it != _operations.end()) {
        return it->second;
    }
    _Operation& op = _operations[key];

    _PjPtr src = _Create(source, &op.error);
    if (!src) {
        op.error = "source: " + op.error;
        return op;
    }
    _PjPtr dst = _Create(target, &op.error);
    if (!dst) {
        op.error = "target: " + op.error;
        return op;
    }

    _FactoryPtr factory = _NewFactory(
        area, PROJ_GRID_AVAILABILITY_DISCARD_OPERATION_IF_MISSING_GRID);
    _ListPtr candidates(proj_create_operations(
        _ctx, src.get(), dst.get(), factory.get()));
    const int count = candidates ? proj_list_get_count(candidates.get()) : 0;
    for (int i = 0; i < count; ++i) {
        _PjPtr candidate(proj_list_get(_ctx, candidates.get(), i));
        if (!candidate ||
            !proj_coordoperation_is_instantiable(_ctx, candidate.get())) {
            continue;
        }
        // Semantic tuple order: longitude first, easting first.
        op.pj.reset(proj_normalize_for_visualization(_ctx, candidate.get()));
        if (!op.pj) {
            continue;
        }
        const char* name = proj_get_name(candidate.get());
        op.description = name ? name : "";
        op.accuracy = _Accuracy(candidate.get());
        op.error.clear();
        op.skipped = _SkippedForMissingGrids(
            src.get(), dst.get(), area, op.accuracy);
        return op;
    }
    op.skipped = _SkippedForMissingGrids(src.get(), dst.get(), area, -1.0);
    op.error = TfStringPrintf(
        "no usable coordinate operation from '%s' to '%s' (approximate "
        "ballpark operations are disabled; operations needing unavailable "
        "grids are discarded)%s%s",
        proj_get_name(src.get()), proj_get_name(dst.get()),
        op.skipped.empty() ? "" : "; operations needing missing grids: ",
        TfStringJoin(op.skipped, "; ").c_str());
    return op;
}

// Operations from src to dst that need grids which are not available and
// are more accurate than the one used (any known accuracy if none is used,
// usedAccuracy < 0): "name (accuracy x m; grid a, b)".
std::vector<std::string>
UsdGeospatialProjEngine::_SkippedForMissingGrids(
    const PJ* src, const PJ* dst, const UsdGeospatialAreaOfInterest& area,
    double usedAccuracy) const
{
    _FactoryPtr factory = _NewFactory(area, PROJ_GRID_AVAILABILITY_IGNORED);

    std::vector<std::string> skipped;
    _ListPtr all(proj_create_operations(_ctx, src, dst, factory.get()));
    const int count = all ? proj_list_get_count(all.get()) : 0;
    for (int i = 0; i < count; ++i) {
        _PjPtr candidate(proj_list_get(_ctx, all.get(), i));
        if (!candidate) {
            continue;
        }
        std::vector<std::string> missing;
        const int grids =
            proj_coordoperation_get_grid_used_count(_ctx, candidate.get());
        for (int g = 0; g < grids; ++g) {
            const char* shortName = nullptr;
            int available = 0;
            if (proj_coordoperation_get_grid_used(
                    _ctx, candidate.get(), g, &shortName, nullptr, nullptr,
                    nullptr, nullptr, nullptr, &available) && !available) {
                missing.push_back(shortName ? shortName : "?");
            }
        }
        const double accuracy = _Accuracy(candidate.get());
        if (missing.empty() || accuracy < 0.0 ||
            (usedAccuracy >= 0.0 && accuracy >= usedAccuracy)) {
            continue;
        }
        const char* name = proj_get_name(candidate.get());
        skipped.push_back(TfStringPrintf(
            "%s (accuracy %g m; grid %s)", name ? name : "?", accuracy,
            TfStringJoin(missing, ", ").c_str()));
    }
    return skipped;
}

// Accuracy in metres, -1 if unknown. PROJ reports -1 for conversions (map
// projections, axis changes), which are exact.
double
UsdGeospatialProjEngine::_Accuracy(const PJ* op) const
{
    const double accuracy = proj_coordoperation_get_accuracy(_ctx, op);
    if (accuracy >= 0.0) {
        return accuracy;
    }
    switch (proj_get_type(op)) {
    case PJ_TYPE_CONVERSION:
        return 0.0;
    case PJ_TYPE_CONCATENATED_OPERATION: {
        const int steps = proj_concatoperation_get_step_count(_ctx, op);
        double total = 0.0;
        for (int i = 0; i < steps; ++i) {
            _PjPtr step(proj_concatoperation_get_step(_ctx, op, i));
            const double a = step ? _Accuracy(step.get()) : -1.0;
            if (a < 0.0) {
                return -1.0;
            }
            total += a;
        }
        return total;
    }
    default:
        return -1.0;
    }
}

UsdGeospatialOperationResult
UsdGeospatialProjEngine::_Result(const _Operation& op) const
{
    UsdGeospatialOperationResult result;
    result.engine = GetName();
    result.operation = op.description;
    result.accuracy = op.accuracy;
    result.error = op.error;
    result.skippedOperations = op.skipped;
    result.succeeded = bool(op.pj);
    return result;
}

UsdGeospatialOperationResult
UsdGeospatialProjEngine::CanTransform(const std::string& source,
                                      const std::string& target,
                                      const UsdGeospatialAreaOfInterest& area)
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _Result(_GetOperation(source, target, area));
}

UsdGeospatialOperationResult
UsdGeospatialProjEngine::Transform(const std::string& source,
                                   const std::string& target,
                                   const UsdGeospatialAreaOfInterest& area,
                                   std::vector<GfVec3d>* points)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const _Operation& op = _GetOperation(source, target, area);
    UsdGeospatialOperationResult result = _Result(op);
    if (!result || points->empty()) {
        return result;
    }

    double* data = points->data()->data();
    const size_t stride = sizeof(GfVec3d);
    proj_errno_reset(op.pj.get());
    proj_trans_generic(op.pj.get(), PJ_FWD,
                       data, stride, points->size(),
                       data + 1, stride, points->size(),
                       data + 2, stride, points->size(),
                       nullptr, 0, 0);
    for (const GfVec3d& p : *points) {
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) ||
            !std::isfinite(p[2]) || p[0] == HUGE_VAL) {
            result.succeeded = false;
            const int err = proj_errno(op.pj.get());
            result.error = TfStringPrintf(
                "'%s' failed for a point: %s", op.description.c_str(),
                err ? proj_context_errno_string(_ctx, err) : "out of range");
            break;
        }
    }
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
