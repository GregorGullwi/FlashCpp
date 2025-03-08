#pragma once

#include <string>
#include <vector>
#include "AstNodeTypes.h"

namespace FlashCpp {

bool GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename);

} // namespace FlashCpp