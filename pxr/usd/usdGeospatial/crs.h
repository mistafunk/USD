//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDGEOSPATIAL_GENERATED_CRS_H
#define USDGEOSPATIAL_GENERATED_CRS_H

/// \file usdGeospatial/crs.h

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/usd/usd/typed.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// GEOSPATIALCRS                                                              //
// -------------------------------------------------------------------------- //

/// \class UsdGeospatialCRS
///
/// Defines a coordinate reference system as a prim, by a complete
/// OGC WKT2 (ISO 19162:2019) string.
/// 
/// A CRS definition can live in the stage or in a shared library layer.
/// Model content uses it through GeospatialCRSBindingAPI: the bound prim
/// composes in a GeospatialCRS prim with an ordinary USD reference (or any
/// other composition arc), so the bound prim's composed crs:wkt is its CRS.
/// Prims below a bound prim inherit its CRS until the next direct binding.
/// 
/// Resolution, placement and coordinate operations are not part of this
/// schema; see the geospatial CRS proposal.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdGeospatialTokens.
/// So to set an attribute to the value "rightHanded", use UsdGeospatialTokens->rightHanded
/// as the value.
///
class UsdGeospatialCRS : public UsdTyped
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::ConcreteTyped;

    /// Construct a UsdGeospatialCRS on UsdPrim \p prim .
    /// Equivalent to UsdGeospatialCRS::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdGeospatialCRS(const UsdPrim& prim=UsdPrim())
        : UsdTyped(prim)
    {
    }

    /// Construct a UsdGeospatialCRS on the prim held by \p schemaObj .
    /// Should be preferred over UsdGeospatialCRS(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdGeospatialCRS(const UsdSchemaBase& schemaObj)
        : UsdTyped(schemaObj)
    {
    }

    /// Destructor.
    USDGEOSPATIAL_API
    virtual ~UsdGeospatialCRS();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDGEOSPATIAL_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdGeospatialCRS holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdGeospatialCRS(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDGEOSPATIAL_API
    static UsdGeospatialCRS
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Attempt to ensure a \a UsdPrim adhering to this schema at \p path
    /// is defined (according to UsdPrim::IsDefined()) on this stage.
    ///
    /// If a prim adhering to this schema at \p path is already defined on this
    /// stage, return that prim.  Otherwise author an \a SdfPrimSpec with
    /// \a specifier == \a SdfSpecifierDef and this schema's prim type name for
    /// the prim at \p path at the current EditTarget.  Author \a SdfPrimSpec s
    /// with \p specifier == \a SdfSpecifierDef and empty typeName at the
    /// current EditTarget for any nonexistent, or existing but not \a Defined
    /// ancestors.
    ///
    /// The given \a path must be an absolute prim path that does not contain
    /// any variant selections.
    ///
    /// If it is impossible to author any of the necessary PrimSpecs, (for
    /// example, in case \a path cannot map to the current UsdEditTarget's
    /// namespace) issue an error and return an invalid \a UsdPrim.
    ///
    /// Note that this method may return a defined prim whose typeName does not
    /// specify this schema class, in case a stronger typeName opinion overrides
    /// the opinion at the current EditTarget.
    ///
    USDGEOSPATIAL_API
    static UsdGeospatialCRS
    Define(const UsdStagePtr &stage, const SdfPath &path);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDGEOSPATIAL_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDGEOSPATIAL_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDGEOSPATIAL_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // WKT 
    // --------------------------------------------------------------------- //
    /// The CRS definition as an OGC WKT2 (ISO 19162:2019) string.
    /// A token, so that definitions compare cheaply. The WKT axes, units and
    /// height reference describe the CRS independently of the stage's
    /// upAxis and metersPerUnit.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token crs:wkt` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDGEOSPATIAL_API
    UsdAttribute GetWktAttr() const;

    /// See GetWktAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDGEOSPATIAL_API
    UsdAttribute CreateWktAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
