//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef PRT_GENERATED_RULEASSIGNMENT_H
#define PRT_GENERATED_RULEASSIGNMENT_H

/// \file prt/ruleAssignment.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/typed.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "./tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// RULEASSIGNMENTPRIM                                                         //
// -------------------------------------------------------------------------- //

/// \class PrtRuleAssignment
///
/// A typed (IsA) schema prim for a CityEngine building model.
///
class PrtRuleAssignment : public UsdTyped
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::AbstractTyped;

    /// Construct a PrtRuleAssignment on UsdPrim \p prim .
    /// Equivalent to PrtRuleAssignment::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit PrtRuleAssignment(const UsdPrim& prim=UsdPrim())
        : UsdTyped(prim)
    {
    }

    /// Construct a PrtRuleAssignment on the prim held by \p schemaObj .
    /// Should be preferred over PrtRuleAssignment(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit PrtRuleAssignment(const UsdSchemaBase& schemaObj)
        : UsdTyped(schemaObj)
    {
    }

    /// Destructor.
    PRT_API
    virtual ~PrtRuleAssignment();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    PRT_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a PrtRuleAssignment holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// PrtRuleAssignment(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    PRT_API
    static PrtRuleAssignment
    Get(const UsdStagePtr &stage, const SdfPath &path);


protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    PRT_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    PRT_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    PRT_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // RULEPACKAGEPATH 
    // --------------------------------------------------------------------- //
    /// An asset path valued attribute that points to the rule package used for this building model.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `asset rulePackagePath` |
    /// | C++ Type | SdfAssetPath |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Asset |
    PRT_API
    UsdAttribute GetRulePackagePathAttr() const;

    /// See GetRulePackagePathAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    PRT_API
    UsdAttribute CreateRulePackagePathAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // RANDOMSEED 
    // --------------------------------------------------------------------- //
    /// Seed value for the pseudo random generator used by PRT for this building model.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `int randomSeed = 0` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    PRT_API
    UsdAttribute GetRandomSeedAttr() const;

    /// See GetRandomSeedAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    PRT_API
    UsdAttribute CreateRandomSeedAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // INITIALSHAPES 
    // --------------------------------------------------------------------- //
    /// geometry used as initial shapes for PRT
    ///
    PRT_API
    UsdRelationship GetInitialShapesRel() const;

    /// See GetInitialShapesRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    PRT_API
    UsdRelationship CreateInitialShapesRel() const;

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
