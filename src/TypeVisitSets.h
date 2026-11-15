#pragma once

#include <functional>

#include "AstNodeTypes_TypeSystem.h"
#include "FlatHashTable.h"
#include "FrontendIds.h"

using TypeIndexSet = FlashCpp::FlatHashSet<TypeIndex, std::hash<TypeIndex>, std::equal_to<TypeIndex>, 8>;
using TypeIdSet = FlashCpp::FlatHashSet<TypeId, std::hash<TypeId>, std::equal_to<TypeId>, 8>;
