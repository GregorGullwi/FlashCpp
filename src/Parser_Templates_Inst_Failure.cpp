#include "Parser.h"

bool Parser::isHardUseLikeInstantiationMode() const {
	return template_instantiation_mode_ == TemplateInstantiationMode::HardUse ||
		   template_instantiation_mode_ == TemplateInstantiationMode::HardUseCandidateProbe;
}

bool Parser::isTemplateInstantiationFailureProbeMode() const {
	return template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe ||
		   template_instantiation_mode_ == TemplateInstantiationMode::HardUseCandidateProbe;
}

TemplateSubstitutionFailurePolicy Parser::currentTemplateSubstitutionFailurePolicy() const {
	if (template_instantiation_mode_ == TemplateInstantiationMode::ShapeOnly) {
		return TemplateSubstitutionFailurePolicy::ShapeOnly;
	}
	if (isTemplateInstantiationFailureProbeMode()) {
		return TemplateSubstitutionFailurePolicy::SfinaeProbe;
	}
	return TemplateSubstitutionFailurePolicy::HardUse;
}

std::optional<ASTNode> Parser::failTemplateInstantiation(
	std::string_view reason,
	const FlashCpp::TemplateInstantiationKey* key,
	std::optional<uintptr_t> overload_id) {
	if (isTemplateInstantiationFailureProbeMode()) {
		if (key != nullptr && overload_id.has_value()) {
			gTemplateRegistry.markFailedInstantiation(*key, *overload_id);
		}
		return std::nullopt;
	}
	// HardUse and ShapeOnly are both non-probe modes: failures are not soft.
	throw CompileError(std::string(reason));
}
