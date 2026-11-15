#pragma once

#include <functional>

#include "AstNodeTypes_DeclNodes.h"
#include "FlatHashTable.h"

using StructInfoSet = FlashCpp::FlatHashSet<const StructTypeInfo*, std::hash<const StructTypeInfo*>, std::equal_to<const StructTypeInfo*>, 8>;
