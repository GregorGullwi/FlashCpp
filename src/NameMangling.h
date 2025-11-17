#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "ChunkedString.h"

// Shared MSVC name mangling utilities used by both CodeGen and ObjectFileWriter

namespace NameMangling {

// Helper to append CV-qualifier code (A/B/C/D) to output
inline void appendCVQualifier(auto& output, CVQualifier cv) {
	if (cv == CVQualifier::None) {
		output += 'A';
	} else if (cv == CVQualifier::Const) {
		output += 'B';
	} else if (cv == CVQualifier::Volatile) {
		output += 'C';
	} else if (cv == CVQualifier::ConstVolatile) {
		output += 'D';
	}
}

// Generate MSVC type code for mangling
// Works with both std::string and StringBuilder
template<typename OutputType>
void appendTypeCode(OutputType& output, const TypeSpecifierNode& type_node) {
	// Handle references - MSVC uses different prefixes for lvalue vs rvalue references
	// Format: [AE|$$QE][A|B|C|D] where A/B/C/D are CV-qualifiers on the REFERENCED type
	if (type_node.is_lvalue_reference()) {
		output += "AE";
		appendCVQualifier(output, type_node.cv_qualifier());
	} else if (type_node.is_rvalue_reference()) {
		output += "$$QE";
		appendCVQualifier(output, type_node.cv_qualifier());
	}

	// Add pointer prefix for each level of indirection with CV-qualifiers
	// MSVC format: [P|Q|R|S][E][A|B|C|D] where:
	//   P = pointer, Q = const pointer, R = volatile pointer, S = const volatile pointer
	//   E = 64-bit (always E for x64)
	//   A = no CV-quals on pointee, B = const pointee, C = volatile pointee, D = const volatile pointee
	const auto& ptr_levels = type_node.pointer_levels();
	for (size_t i = 0; i < ptr_levels.size(); ++i) {
		const auto& ptr_level = ptr_levels[i];
		
		// Pointer CV-qualifiers (on the pointer itself)
		if (ptr_level.cv_qualifier == CVQualifier::None) {
			output += "PE";
		} else if (ptr_level.cv_qualifier == CVQualifier::Const) {
			output += "QE";
		} else if (ptr_level.cv_qualifier == CVQualifier::Volatile) {
			output += "RE";
		} else if (ptr_level.cv_qualifier == CVQualifier::ConstVolatile) {
			output += "SE";
		}
		
		// Pointee CV-qualifiers (on what the pointer points to)
		// For the last pointer level, use the base type's CV-qualifier
		// For intermediate levels, get CV from the next pointer level
		CVQualifier pointee_cv = (i == ptr_levels.size() - 1) 
			? type_node.cv_qualifier() 
			: ptr_levels[i + 1].cv_qualifier;
			
		appendCVQualifier(output, pointee_cv);
	}

	// Add base type code
	switch (type_node.type()) {
		case Type::Void: output += 'X'; break;
		case Type::Bool: output += "_N"; break;  // bool
		case Type::Char: output += 'D'; break;   // char
		case Type::UnsignedChar: output += 'E'; break;  // unsigned char
		case Type::Short: output += 'F'; break;  // short
		case Type::UnsignedShort: output += 'G'; break;  // unsigned short
		case Type::Int: output += 'H'; break;    // int
		case Type::UnsignedInt: output += 'I'; break;  // unsigned int
		case Type::Long: output += 'J'; break;   // long
		case Type::UnsignedLong: output += 'K'; break;  // unsigned long
		case Type::LongLong: output += "_J"; break;  // long long
		case Type::UnsignedLongLong: output += "_K"; break;  // unsigned long long
		case Type::Float: output += 'M'; break;  // float
		case Type::Double: output += 'N'; break;  // double
		case Type::LongDouble: output += 'O'; break;  // long double
		case Type::Struct:
		case Type::UserDefined: {
			// Struct/class types use format: V<name>@@ or U<name>@@ (V for class, U for struct, but we use V)
			// Get the type name from the global type registry
			if (type_node.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
				output += 'V';
				output += type_info.name_;
				output += "@@";
			} else {
				output += 'H';  // Fallback to int if type not found
			}
			break;
		}
		default: output += 'H'; break;  // Default to int for unknown types
	}
}

} // namespace NameMangling
