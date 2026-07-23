#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "BuiltinListInitNarrowing.h"
#include "CallNodeHelpers.h"
#include "ExpressionSubstitutor.h"
#include "MemberFunctionLookupShared.h"
#include "OverloadResolution.h"
#include "SemanticAnalysis.h"
#include "TypeTraitEvaluator.h"
#include <algorithm>
#include <limits>

#include "ConstExprEvalHelpers.h"

namespace ConstExpr {

// Evaluate qualified identifier (e.g., Namespace::var or Template<T>::member)
EvalResult Evaluator::evaluate_qualified_identifier(const QualifiedIdentifierNode& qualified_id, EvaluationContext& context) {
	// Look up the qualified name in the symbol table
	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate qualified identifier: no symbol table provided");
	}

	struct QualifiedVariableTemplateArgsResult {
		std::vector<TemplateTypeArg> template_args;
		std::optional<EvalResult> error;
		bool has_template_args = false;
	};

	auto collectQualifiedVariableTemplateArgs =
		[&](const TypeInfo* lookup_owner_type) -> QualifiedVariableTemplateArgsResult {
		QualifiedVariableTemplateArgsResult result;
		if (qualified_id.has_template_arguments()) {
			result.has_template_args = true;
			result.template_args.reserve(qualified_id.template_arguments().size());
			for (const ASTNode& arg_node : qualified_id.template_arguments()) {
				if (arg_node.is<TypeSpecifierNode>()) {
					result.template_args.emplace_back(arg_node.as<TypeSpecifierNode>());
					continue;
				}
				if (!arg_node.is<ExpressionNode>()) {
					result.error = EvalResult::error(
						"Unsupported template argument type for qualified variable template");
					return result;
				}

				EvalResult arg_val = evaluate(arg_node, context);
				if (!arg_val.success()) {
					result.error = EvalResult::error(
						"Failed to evaluate non-type template argument: " +
						arg_val.error_message);
					return result;
				}
				result.template_args.push_back(templateTypeArgFromEvalResult(arg_val));
			}
			return result;
		}

		const TypeInfo::DependentQualifiedNameRecord* dependent_record =
			qualified_id.dependentQualifiedName();
		if (dependent_record == nullptr ||
			dependent_record->member_chain.empty()) {
			return result;
		}

		const auto& final_member = dependent_record->member_chain.back();
		if (!final_member.has_template_arguments) {
			return result;
		}

		result.has_template_args = true;
		result.template_args.reserve(final_member.template_arguments.size());
		for (const TypeInfo::TemplateArgInfo& stored_arg : final_member.template_arguments) {
			TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
			if (std::optional<TemplateTypeArg> rebound_arg =
					trySubstituteDependentTemplateArgForLookup(
						arg,
						context,
						lookup_owner_type,
						0);
				rebound_arg.has_value()) {
				arg = std::move(*rebound_arg);
			}
			if (arg.is_value && arg.dependent_expr.has_value()) {
				EvalResult arg_val = evaluate(*arg.dependent_expr, context);
				if (!arg_val.success()) {
					result.error = EvalResult::error(
						"Failed to evaluate non-type template argument: " +
						arg_val.error_message);
					return result;
				}
				arg = templateTypeArgFromEvalResult(arg_val);
			}
			result.template_args.push_back(std::move(arg));
		}
		return result;
	};

	auto tryInstantiateQualifiedMemberVariableTemplate =
		[&](const TypeInfo* lookup_owner_type) -> std::optional<EvalResult> {
		if (context.parser == nullptr) {
			return std::nullopt;
		}

		QualifiedVariableTemplateArgsResult template_arg_result =
			collectQualifiedVariableTemplateArgs(lookup_owner_type);
		if (template_arg_result.error.has_value()) {
			return *template_arg_result.error;
		}
		if (!template_arg_result.has_template_args ||
			template_arg_result.template_args.empty()) {
			return std::nullopt;
		}
		std::vector<TemplateTypeArg>& template_args =
			template_arg_result.template_args;

		Parser& parser = *context.parser;
		StringHandle owner_handle{};
		if (lookup_owner_type != nullptr && lookup_owner_type->name().isValid()) {
			owner_handle = lookup_owner_type->name();
		} else {
			NamespaceHandle ns_handle = qualified_id.namespace_handle();
			if (!ns_handle.isGlobal()) {
				owner_handle = gNamespaceRegistry.getQualifiedNameHandle(ns_handle);
				if (!owner_handle.isValid()) {
					std::string_view owner_name =
						gNamespaceRegistry.getQualifiedName(ns_handle);
					if (!owner_name.empty()) {
						owner_handle =
							StringTable::getOrInternStringHandle(owner_name);
					}
				}
			}
		}
		std::string_view variable_template_lookup_name;
		std::optional<OuterTemplateBinding> explicit_outer_binding;
		if (owner_handle.isValid()) {
			variable_template_lookup_name =
				parser.lookup_inherited_member_variable_template_name(
					owner_handle,
					qualified_id.nameHandle(),
					0);
			if (variable_template_lookup_name.empty()) {
				StringBuilder qualified_name_builder;
				variable_template_lookup_name = qualified_name_builder
					.append(StringTable::getStringView(owner_handle))
					.append("::")
					.append(qualified_id.name())
					.commit();
			}
			explicit_outer_binding =
				parser.buildOuterBindingForOwner(owner_handle);
		} else {
			variable_template_lookup_name = qualified_id.name();
		}

		std::optional<ASTNode> instantiated_var =
			parser.try_instantiate_variable_template(
				variable_template_lookup_name,
				template_args,
				explicit_outer_binding.has_value()
					? &*explicit_outer_binding
					: nullptr);
		context.normalizePendingSemanticRoots();
		if (!instantiated_var.has_value() &&
			variable_template_lookup_name != qualified_id.name()) {
			instantiated_var =
				parser.try_instantiate_variable_template(
					qualified_id.name(),
					template_args,
					explicit_outer_binding.has_value()
						? &*explicit_outer_binding
						: nullptr);
			context.normalizePendingSemanticRoots();
		}
		if (!instantiated_var.has_value() ||
			!instantiated_var->is<VariableDeclarationNode>()) {
			return std::nullopt;
		}

		const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
		if (!var_decl.initializer().has_value()) {
			return std::nullopt;
		}
		return evaluate(var_decl.initializer().value(), context);
	};

	// Try to look up the qualified name
	auto symbol_opt = context.symbols->lookup_qualified(qualified_id.qualifiedIdentifier());
	if (!symbol_opt.has_value()) {
		if (std::optional<EvalResult> variable_template_value =
				tryInstantiateQualifiedMemberVariableTemplate(nullptr);
			variable_template_value.has_value()) {
			return *variable_template_value;
		}

		auto resolveDependentOwnerHandle = [&]() -> StringHandle {
			const TypeInfo::DependentQualifiedNameRecord* dependent_record =
				qualified_id.dependentQualifiedName();
			if (dependent_record == nullptr) {
				return {};
			}
			auto materializeOwnerMemberChainPrefix = [&](StringHandle owner_handle) -> StringHandle {
				if (!owner_handle.isValid() ||
					context.parser == nullptr ||
					dependent_record->member_chain.size() <= 1) {
					return owner_handle;
				}

				std::vector<QualifiedTypeMemberAccess> member_type_chain;
				member_type_chain.reserve(dependent_record->member_chain.size() - 1);
				for (size_t member_index = 0;
					 member_index + 1 < dependent_record->member_chain.size();
					 ++member_index) {
					const auto& member_record =
						dependent_record->member_chain[member_index];
					QualifiedTypeMemberAccess member_access;
					member_access.member_name = member_record.name;
					if (member_record.has_template_arguments) {
						std::vector<TemplateTypeArg> member_args;
						member_args.reserve(member_record.template_arguments.size());
						for (const TypeInfo::TemplateArgInfo& stored_arg :
							 member_record.template_arguments) {
							TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
							if (std::optional<TemplateTypeArg> rebound_arg =
									trySubstituteDependentTemplateArgForLookup(
										arg,
										context,
										nullptr,
										0);
								rebound_arg.has_value()) {
								arg = std::move(*rebound_arg);
							}
							if (arg.is_value && arg.dependent_expr.has_value()) {
								EvalResult evaluated_arg = evaluate(
									*arg.dependent_expr,
									context);
								if (!evaluated_arg.success()) {
									return owner_handle;
								}
								arg = templateTypeArgFromEvalResult(evaluated_arg);
							}
							if (arg.is_dependent) {
								return owner_handle;
							}
							member_args.push_back(std::move(arg));
						}
						member_access.has_template_arguments = true;
						member_access.template_arguments =
							&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(
								std::move(member_args));
					}
					member_type_chain.push_back(std::move(member_access));
				}

				const TypeInfo* resolved_owner_type =
					context.parser->resolveBaseClassMemberTypeChain(
						StringTable::getStringView(owner_handle),
						member_type_chain);
				if (resolved_owner_type == nullptr &&
					context.struct_info != nullptr &&
					!context.struct_info->base_classes.empty()) {
					for (const auto& base_class : context.struct_info->base_classes) {
						if (!base_class.type_index.is_valid()) {
							continue;
						}
						const TypeInfo* base_type_info =
							tryGetTypeInfo(base_class.type_index);
						if (base_type_info == nullptr ||
							!base_type_info->name().isValid()) {
							continue;
						}
						resolved_owner_type =
							context.parser->resolveBaseClassMemberTypeChain(
								StringTable::getStringView(base_type_info->name()),
								member_type_chain);
						if (resolved_owner_type != nullptr) {
							break;
						}
					}
				}
				if (resolved_owner_type != nullptr &&
					resolved_owner_type->name().isValid()) {
					return resolved_owner_type->name();
				}
				return owner_handle;
			};
			auto canonicalOwnerHandleForArg = [&](const TemplateTypeArg& owner_arg) -> StringHandle {
				if (owner_arg.is_value || owner_arg.is_dependent) {
					return {};
				}
				if (context.parser != nullptr) {
					Parser::AliasTemplateMaterializationResult canonical_owner =
						context.parser->materializeCanonicalOwnerTypeForLookup(owner_arg);
					if (canonical_owner.canonicalNameHandle().isValid()) {
						return canonical_owner.canonicalNameHandle();
					}
					if (canonical_owner.resolved_type_info != nullptr &&
						canonical_owner.resolved_type_info->name().isValid()) {
						return canonical_owner.resolved_type_info->name();
					}
				}
				if (owner_arg.type_index.is_valid()) {
					if (const TypeInfo* owner_type_info = tryGetTypeInfo(owner_arg.type_index);
						owner_type_info != nullptr && owner_type_info->name().isValid()) {
						return owner_type_info->name();
					}
				}
				return {};
			};

			switch (dependent_record->owner_kind) {
			case TypeInfo::DependentQualifiedNameRecord::OwnerKind::CurrentInstantiation:
				if (dependent_record->owner_type.is_valid()) {
					if (const TypeInfo* owner_type_info =
							tryGetTypeInfo(dependent_record->owner_type);
						owner_type_info != nullptr && owner_type_info->name().isValid()) {
						return materializeOwnerMemberChainPrefix(owner_type_info->name());
					}
				}
				if (dependent_record->owner_name.isValid()) {
					if (context.parser != nullptr) {
						Parser::ResolvedQualifiedOwner resolved_owner =
							context.parser->resolveQualifiedOwnerForLookup(
								StringTable::getStringView(
									dependent_record->owner_name));
						if (resolved_owner.resolved_from_current_context &&
							resolved_owner.lookupNameHandle().isValid()) {
							return materializeOwnerMemberChainPrefix(
								resolved_owner.lookupNameHandle());
						}
					}
					return materializeOwnerMemberChainPrefix(dependent_record->owner_name);
				}
				if (context.struct_info != nullptr && context.struct_info->name.isValid()) {
					return materializeOwnerMemberChainPrefix(context.struct_info->name);
				}
				break;
			case TypeInfo::DependentQualifiedNameRecord::OwnerKind::TemplateParameter:
			case TypeInfo::DependentQualifiedNameRecord::OwnerKind::DependentInstantiation:
			case TypeInfo::DependentQualifiedNameRecord::OwnerKind::UnknownSpecialization:
				if (dependent_record->owner_name.isValid()) {
					if (std::optional<TemplateTypeArg> bound_owner =
							resolveContextBinding(
								dependent_record->owner_name,
								context.template_environment);
						bound_owner.has_value()) {
						if (StringHandle owner_handle = canonicalOwnerHandleForArg(*bound_owner);
							owner_handle.isValid()) {
							return materializeOwnerMemberChainPrefix(owner_handle);
						}
					}
				}
				break;
			}
			return {};
		};

		// PHASE 3 FIX: If not found in symbol table, try looking up as struct static member
		// This handles cases like is_pointer_impl<int*>::value where value is a static member
		// Also handles type aliases like `using my_true = integral_constant<bool, true>; my_true::value`
		NamespaceHandle ns_handle = qualified_id.namespace_handle();
		StringHandle struct_handle;
		if (StringHandle dependent_owner_handle = resolveDependentOwnerHandle();
			dependent_owner_handle.isValid()) {
			struct_handle = dependent_owner_handle;
		}
		const bool qualifier_is_known_namespace =
			ns_handle.isValid() &&
			!ns_handle.isGlobal() &&
			context.symbols->has_namespace_symbols(ns_handle);

		if (!struct_handle.isValid() && !ns_handle.isGlobal()) {
			struct_handle = gNamespaceRegistry.getQualifiedNameHandle(ns_handle);
			if (!struct_handle.isValid()) {
				struct_handle = StringTable::getOrInternStringHandle(gNamespaceRegistry.getName(ns_handle));
			}
		}

		// If we still don't have a struct name, derive it from the qualified identifier.
		// Example: "std::is_integral<int>::value" -> "std::is_integral<int>"
		if (!struct_handle.isValid()) {
			std::string_view ns_name = gNamespaceRegistry.getQualifiedName(ns_handle);
			if (!ns_name.empty()) {
				struct_handle = StringTable::getOrInternStringHandle(ns_name);
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Extracted struct_name='", ns_name, "' from qualified namespace");
				}
			}
		}

		if (struct_handle.isValid() && context.parser != nullptr) {
			std::string_view qualified_owner_name = StringTable::getStringView(struct_handle);
			size_t last_scope = qualified_owner_name.rfind("::");
			if (last_scope != std::string_view::npos &&
				last_scope + 2 < qualified_owner_name.size()) {
				StringHandle outer_owner_handle = StringTable::getOrInternStringHandle(
					qualified_owner_name.substr(0, last_scope));
				StringHandle nested_member_handle = StringTable::getOrInternStringHandle(
					qualified_owner_name.substr(last_scope + 2));
				if (const TypeInfo* inherited_alias_owner =
						context.parser->lookup_inherited_type_alias(
							outer_owner_handle,
							nested_member_handle,
							0);
					inherited_alias_owner != nullptr &&
					inherited_alias_owner->name().isValid()) {
					ResolvedAliasTypeInfo resolved_owner_alias = resolveAliasTypeInfo(
						inherited_alias_owner->registeredTypeIndex().withCategory(
							inherited_alias_owner->typeEnum()));
					if (resolved_owner_alias.terminal_type_info != nullptr &&
						resolved_owner_alias.terminal_type_info->name().isValid()) {
						struct_handle = resolved_owner_alias.terminal_type_info->name();
					} else {
						struct_handle = inherited_alias_owner->name();
					}
				}
			}

			std::string_view chain_full_name = gNamespaceRegistry.getQualifiedName(ns_handle);
			size_t first_colon = chain_full_name.find("::");
			if (first_colon != std::string_view::npos &&
				first_colon + 2 < chain_full_name.size()) {
				std::string_view base_name = chain_full_name.substr(0, first_colon);
				std::string_view member_chain = chain_full_name.substr(first_colon + 2);
				std::vector<QualifiedTypeMemberAccess> parsed_member_chain =
					context.parser->buildQualifiedTypeMemberChain(member_chain);
				if (!parsed_member_chain.empty()) {
					std::vector<std::vector<TemplateTypeArg>> member_template_arg_storage(
						parsed_member_chain.size());
					if (const TypeInfo::DependentQualifiedNameRecord* dependent_record =
							qualified_id.dependentQualifiedName();
						dependent_record != nullptr &&
						dependent_record->member_chain.size() >= parsed_member_chain.size()) {
						for (size_t i = 0; i < parsed_member_chain.size(); ++i) {
							const auto& dep_member = dependent_record->member_chain[i];
							if (dep_member.name != parsed_member_chain[i].member_name) {
								break;
							}
							if (!dep_member.has_template_arguments) {
								continue;
							}
							auto& args_storage = member_template_arg_storage[i];
							args_storage.reserve(dep_member.template_arguments.size());
							for (const TypeInfo::TemplateArgInfo& stored_arg : dep_member.template_arguments) {
								TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
								if (std::optional<TemplateTypeArg> rebound_arg =
										trySubstituteDependentTemplateArgForLookup(
											arg,
											context,
											nullptr,
											0);
									rebound_arg.has_value()) {
									arg = std::move(*rebound_arg);
								}
								if (arg.is_value && arg.dependent_expr.has_value()) {
									EvalResult evaluated_arg = evaluate(*arg.dependent_expr, context);
									if (!evaluated_arg.success()) {
										args_storage.clear();
										break;
									}
									arg = templateTypeArgFromEvalResult(evaluated_arg);
								}
								if (arg.is_dependent) {
									args_storage.clear();
									break;
								}
								args_storage.push_back(std::move(arg));
							}
							if (!args_storage.empty()) {
								parsed_member_chain[i].has_template_arguments = true;
								parsed_member_chain[i].template_arguments = &args_storage;
							}
						}
					}
					if (const TypeInfo* resolved_chain_owner =
							context.parser->resolveBaseClassMemberTypeChain(
								base_name,
								parsed_member_chain);
						resolved_chain_owner != nullptr &&
						resolved_chain_owner->name().isValid()) {
						struct_handle = resolved_chain_owner->name();
					} else if (context.struct_info != nullptr) {
						if (context.struct_info->name.isValid()) {
							if (const TypeInfo* resolved_from_current =
									context.parser->resolveBaseClassMemberTypeChain(
										StringTable::getStringView(context.struct_info->name),
										parsed_member_chain);
								resolved_from_current != nullptr &&
								resolved_from_current->name().isValid()) {
								struct_handle = resolved_from_current->name();
							}
						}
						if (struct_handle == StringTable::getOrInternStringHandle(chain_full_name) &&
							!context.struct_info->base_classes.empty()) {
							for (const auto& base_class : context.struct_info->base_classes) {
								if (!base_class.type_index.is_valid()) {
									continue;
								}
								const TypeInfo* base_type_info = tryGetTypeInfo(base_class.type_index);
								if (base_type_info == nullptr || !base_type_info->name().isValid()) {
									continue;
								}
								if (const TypeInfo* resolved_from_base =
										context.parser->resolveBaseClassMemberTypeChain(
											StringTable::getStringView(base_type_info->name()),
											parsed_member_chain);
									resolved_from_base != nullptr &&
									resolved_from_base->name().isValid()) {
									struct_handle = resolved_from_base->name();
									break;
								}
							}
						}
					}
				}
			}
		}

		if (struct_handle.isValid()) {
			if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
				FLASH_LOG(ConstExpr, Debug, "Looking up struct '", StringTable::getStringView(struct_handle), "' for member '", qualified_id.name(), "'");
			}

			// Look up the struct in getTypesByNameMap()
			auto struct_type_it = getTypesByNameMap().find(struct_handle);
			const TypeInfo* chain_resolved_owner_type = nullptr;

			// If not found with the full qualified name (e.g., "std::is_integral$hash"),
			// try without the namespace prefix (e.g., "is_integral$hash") since template
			// instantiations are often registered with just the short name
			if (struct_type_it == getTypesByNameMap().end()) {
				std::string_view full_name = StringTable::getStringView(struct_handle);
				size_t last_colon = full_name.rfind("::");
				if (last_colon != std::string_view::npos) {
					std::string_view short_name = full_name.substr(last_colon + 2);
					StringHandle short_handle = StringTable::getOrInternStringHandle(short_name);
					struct_type_it = getTypesByNameMap().find(short_handle);
					if (struct_type_it != getTypesByNameMap().end()) {
						FLASH_LOG(ConstExpr, Debug, "Found type using short name '", short_name, "'");
					}
				}
				if (struct_type_it == getTypesByNameMap().end() &&
					context.parser != nullptr) {
					std::string_view chain_full_name = StringTable::getStringView(struct_handle);
					size_t first_colon = chain_full_name.find("::");
					if (first_colon != std::string_view::npos &&
						first_colon + 2 < chain_full_name.size()) {
						std::string_view base_name = chain_full_name.substr(0, first_colon);
						std::string_view member_chain = chain_full_name.substr(first_colon + 2);
						std::vector<QualifiedTypeMemberAccess> parsed_member_chain =
							context.parser->buildQualifiedTypeMemberChain(member_chain);
						if (const TypeInfo* resolved_owner =
								context.parser->resolveBaseClassMemberTypeChain(
									base_name,
									parsed_member_chain);
							resolved_owner != nullptr) {
							chain_resolved_owner_type = resolved_owner;
							if (resolved_owner->name().isValid()) {
								struct_handle = resolved_owner->name();
								auto rebound_it = getTypesByNameMap().find(struct_handle);
								if (rebound_it != getTypesByNameMap().end()) {
									struct_type_it = rebound_it;
								}
							}
						} else if (context.struct_info != nullptr &&
								   context.struct_info->name.isValid()) {
							if (const TypeInfo* resolved_from_current =
									context.parser->resolveBaseClassMemberTypeChain(
										StringTable::getStringView(context.struct_info->name),
										parsed_member_chain);
								resolved_from_current != nullptr &&
								resolved_from_current->name().isValid()) {
								chain_resolved_owner_type = resolved_from_current;
								struct_handle = resolved_from_current->name();
								auto rebound_it = getTypesByNameMap().find(struct_handle);
								if (rebound_it != getTypesByNameMap().end()) {
									struct_type_it = rebound_it;
								}
							}
						}
					}
				}
			}

			// If not found directly, this might be a type alias
			// Type aliases are registered with their alias name pointing to the underlying type
			const StructTypeInfo* struct_info = nullptr;
			const TypeInfo* resolved_type_info = nullptr;

			if (struct_type_it != getTypesByNameMap().end() ||
				chain_resolved_owner_type != nullptr) {
				const TypeInfo* type_info =
					chain_resolved_owner_type != nullptr
						? chain_resolved_owner_type
						: struct_type_it->second;

				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Found type_info, isStruct=", type_info->isStruct(),
							  ", type_index=", type_info->type_index_, ", hasStructInfo=", (type_info->getStructInfo() != nullptr),
							  ", hasInstCtx=", type_info->hasInstantiationContext());
				}

				// A constexpr qualified-id can name a dependent template-instantiation
				// placeholder whose arguments are now concrete in the current default-NTTP
				// context, but whose StructTypeInfo has not been materialized yet.
				// Materialize that owner before looking for static members such as
				// `__empty_not_final<_Head>::value`.
				if (type_info->isTemplateInstantiation() &&
					type_info->getStructInfo() == nullptr &&
					context.parser != nullptr) {
					Parser& parser = *context.parser;
					auto materialize_template_arg_for_owner =
						[&](auto&& self,
							const TemplateTypeArg& source_arg,
							const TypeInfo* source_owner,
							int depth) -> TemplateTypeArg {
						constexpr int kMaxOwnerArgMaterializationDepth = 8;
						TemplateTypeArg concrete_arg = source_arg;
						if (std::optional<TemplateTypeArg> rebound_arg =
								trySubstituteDependentTemplateArgForLookup(
									concrete_arg,
									context,
									source_owner,
									depth);
							rebound_arg.has_value()) {
							concrete_arg = std::move(*rebound_arg);
						}

						if (depth >= kMaxOwnerArgMaterializationDepth ||
							concrete_arg.is_value ||
							!concrete_arg.type_index.is_valid()) {
							return concrete_arg;
						}

						const TypeInfo* concrete_type_info = tryGetTypeInfo(concrete_arg.type_index);
						if (concrete_type_info == nullptr ||
							!concrete_type_info->isTemplateInstantiation()) {
							return concrete_arg;
						}

						std::string_view nested_base_template_name =
							StringTable::getStringView(concrete_type_info->baseTemplateName());
						if (nested_base_template_name.empty()) {
							return concrete_arg;
						}
						StringHandle nested_qualified_template_name =
							gNamespaceRegistry.buildQualifiedIdentifier(
								concrete_type_info->sourceNamespace(),
								concrete_type_info->baseTemplateName());
						std::string_view nested_template_name_for_materialization =
							nested_base_template_name;
						if (nested_qualified_template_name.isValid()) {
							TemplateNameLookupResult nested_qualified_lookup =
								gTemplateRegistry.lookupTemplateName(
									parser.makeTemplateNameLookupRequest(
										nested_qualified_template_name,
										TemplateNameLookupKind::Qualified,
										false));
							if (nested_qualified_lookup.hasClassTemplate()) {
								nested_template_name_for_materialization =
									StringTable::getStringView(
										nested_qualified_template_name);
							}
						}

						std::vector<TemplateTypeArg> nested_concrete_args;
						nested_concrete_args.reserve(concrete_type_info->templateArgs().size());
						for (const auto& nested_arg_info : concrete_type_info->templateArgs()) {
							TemplateTypeArg nested_arg = self(
								self,
								toTemplateTypeArg(nested_arg_info),
								concrete_type_info,
								depth + 1);
							nested_concrete_args.push_back(std::move(nested_arg));
						}

						Parser::AliasTemplateMaterializationResult nested_materialized_type =
							parser.materializeTemplateInstantiationForLookup(
								nested_template_name_for_materialization,
								nested_concrete_args);
						const TypeInfo* nested_resolved_info =
							nested_materialized_type.resolved_type_info;
						if (nested_resolved_info == nullptr &&
							!nested_materialized_type.instantiated_name.empty()) {
							nested_resolved_info = findTypeByName(
								StringTable::getOrInternStringHandle(
									nested_materialized_type.instantiated_name));
						}
						if (nested_resolved_info == nullptr) {
							return concrete_arg;
						}

						TemplateTypeArg resolved_type_arg;
						resolved_type_arg.type_index =
							nested_resolved_info->registeredTypeIndex().withCategory(
								nested_resolved_info->typeEnum());
						resolved_type_arg.setCategory(nested_resolved_info->typeEnum());
						return rebindDependentTemplateTypeArg(
							resolved_type_arg,
							concrete_arg);
					};
					std::string_view base_template_name =
						StringTable::getStringView(type_info->baseTemplateName());
					if (!base_template_name.empty()) {
						StringHandle qualified_template_name =
							gNamespaceRegistry.buildQualifiedIdentifier(
								type_info->sourceNamespace(),
								type_info->baseTemplateName());
						std::string_view template_name_for_materialization =
							base_template_name;
						if (qualified_template_name.isValid()) {
							TemplateNameLookupResult qualified_lookup =
								gTemplateRegistry.lookupTemplateName(
									parser.makeTemplateNameLookupRequest(
										qualified_template_name,
										TemplateNameLookupKind::Qualified,
										false));
							if (qualified_lookup.hasClassTemplate()) {
								template_name_for_materialization =
									StringTable::getStringView(
										qualified_template_name);
							}
						}
						std::vector<TemplateTypeArg> concrete_args;
						concrete_args.reserve(type_info->templateArgs().size());
						for (const auto& arg_info : type_info->templateArgs()) {
							TemplateTypeArg concrete_arg = materialize_template_arg_for_owner(
								materialize_template_arg_for_owner,
								toTemplateTypeArg(arg_info),
								type_info,
								0);
							concrete_args.push_back(std::move(concrete_arg));
						}

						Parser::AliasTemplateMaterializationResult materialized_type =
							parser.materializeTemplateInstantiationForLookup(
								template_name_for_materialization,
								concrete_args);
						if (materialized_type.resolved_type_info != nullptr) {
							type_info = materialized_type.resolved_type_info;
							FLASH_LOG(ConstExpr, Debug, "Materialized constexpr qualified-id owner through template args");
						} else if (!materialized_type.instantiated_name.empty()) {
							auto materialized_it = getTypesByNameMap().find(
								StringTable::getOrInternStringHandle(materialized_type.instantiated_name));
							if (materialized_it != getTypesByNameMap().end() &&
								materialized_it->second != nullptr) {
								type_info = materialized_it->second;
								FLASH_LOG(ConstExpr, Debug, "Resolved constexpr qualified-id owner by materialized name");
							}
						}
					}
				}

				// Follow the type alias chain until we find a struct with actual StructTypeInfo
				// Type aliases may have isStruct()=true but getStructInfo()=null
				// Limit iterations to prevent infinite loops from cycles
				constexpr size_t MAX_ALIAS_CHAIN_DEPTH = 100;
				size_t alias_depth = 0;
				while (type_info && alias_depth < MAX_ALIAS_CHAIN_DEPTH) {
					// Check if we already have StructInfo - if so, we're done
					if (type_info->isStruct() && type_info->getStructInfo() != nullptr) {
						break;
					}
					// Follow alias chains first, then fall back to raw type_index_.
					const TypeInfo* underlying = nullptr;
					ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
						type_info->registeredTypeIndex().withCategory(
							type_info->typeEnum()));
					if (resolved_alias.terminal_type_info != nullptr &&
						resolved_alias.terminal_type_info != type_info) {
						underlying = resolved_alias.terminal_type_info;
					} else if (resolved_alias.type_index.is_valid()) {
						const TypeInfo* alias_type = tryGetTypeInfo(resolved_alias.type_index);
						if (alias_type != nullptr &&
							alias_type != type_info) {
							underlying = alias_type;
						}
					}
					if (underlying == nullptr) {
						underlying = tryGetTypeInfo(type_info->type_index_);
					}
					if (!underlying)
						break;
					if (underlying == type_info)
						break;  // Avoid direct self-reference
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Following type alias to index ", type_info->type_index_);
					}
					type_info = underlying;
					++alias_depth;
				}

				if (type_info && type_info->isStruct()) {
					struct_info = type_info->getStructInfo();
					resolved_type_info = type_info;
				} else if (type_info) {
					if (const EnumTypeInfo* enum_info = type_info->getEnumInfo()) {
						if (auto enum_result = tryEvaluateEnumConstant(
								*enum_info,
								type_info->type_index_,
								qualified_id.nameHandle())) {
							return *enum_result;
						}
					}
				}
			}

			if (struct_info) {
				// Look for static member recursively (checks base classes too)
				StringHandle member_handle = StringTable::getOrInternStringHandle(qualified_id.name());
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Static lookup in struct '", StringTable::getStringView(struct_handle), "', bases=", struct_info->base_classes.size());
					if (resolved_type_info) {
						FLASH_LOG(ConstExpr, Debug, "Resolved type base template='", StringTable::getStringView(resolved_type_info->baseTemplateName()), "', template args=", resolved_type_info->templateArgs().size());
						for (size_t i = 0; i < resolved_type_info->templateArgs().size(); ++i) {
							const auto& arg = resolved_type_info->templateArgs()[i];
							FLASH_LOG(ConstExpr, Debug, "  resolved arg[", i, "] is_value=", arg.is_value, ", category=", static_cast<int>(arg.category()), ", type_index=", arg.type_index, ", value(int)=", arg.intValue());
						}
					}
					for (const auto& base : struct_info->base_classes) {
						if (const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index)) {
							FLASH_LOG(ConstExpr, Debug, "  base type_index=", base.type_index, " name='", StringTable::getStringView(base_type_info->name_), "'");
						}
					}
					FLASH_LOG(ConstExpr, Debug, "  static members=", struct_info->static_members.size(), ", non-static members=", struct_info->members.size());
					for (const auto& static_member : struct_info->static_members) {
						FLASH_LOG(ConstExpr, Debug, "    static member name='", StringTable::getStringView(static_member.getName()), "'");
					}
					for (const auto& member : struct_info->members) {
						FLASH_LOG(ConstExpr, Debug, "    member name='", StringTable::getStringView(member.name), "'");
					}
				}

				auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
				TypeIndex pre_materialization_type_index = resolved_type_info != nullptr ? resolved_type_info->type_index_ : TypeIndex{};
				if (static_member != nullptr &&
					owner_struct == struct_info &&
					context.parser != nullptr) {
					std::vector<QualifiedTypeMemberAccess> parsed_member_chain;
					std::vector<std::vector<TemplateTypeArg>> member_template_arg_storage;
					StringHandle dependent_owner_handle;
					bool has_member_chain = false;
					if (const auto* dependent_record = qualified_id.dependentQualifiedName();
						dependent_record != nullptr &&
						!dependent_record->member_chain.empty()) {
						dependent_owner_handle = dependent_record->owner_name;
						if (dependent_record->member_chain.size() > 1) {
							parsed_member_chain.reserve(dependent_record->member_chain.size() - 1);
							member_template_arg_storage.resize(dependent_record->member_chain.size() - 1);
							for (size_t member_index = 0;
								 member_index + 1 < dependent_record->member_chain.size();
								 ++member_index) {
								const auto& member_record = dependent_record->member_chain[member_index];
								QualifiedTypeMemberAccess member_access;
								member_access.member_name = member_record.name;
								member_access.has_template_arguments = false;
								if (member_record.has_template_arguments) {
									auto& template_arg_storage = member_template_arg_storage[member_index];
									template_arg_storage.reserve(member_record.template_arguments.size());
									for (const TypeInfo::TemplateArgInfo& stored_arg : member_record.template_arguments) {
										TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
										if (std::optional<TemplateTypeArg> rebound_arg =
												trySubstituteDependentTemplateArgForLookup(
													arg,
													context,
													nullptr,
													0);
											rebound_arg.has_value()) {
											arg = std::move(*rebound_arg);
										}
										if (arg.is_value && arg.dependent_expr.has_value()) {
											EvalResult evaluated_arg = evaluate(*arg.dependent_expr, context);
											if (!evaluated_arg.success()) {
												template_arg_storage.clear();
												break;
											}
											arg = templateTypeArgFromEvalResult(evaluated_arg);
										}
										if (arg.is_dependent) {
											template_arg_storage.clear();
											break;
										}
										template_arg_storage.push_back(std::move(arg));
									}
									if (!template_arg_storage.empty()) {
										member_access.has_template_arguments = true;
										member_access.template_arguments = &template_arg_storage;
									}
								}
								parsed_member_chain.push_back(member_access);
							}
							has_member_chain = !parsed_member_chain.empty();
						}
					}
					if (!has_member_chain) {
						std::string_view ns_full_name =
							gNamespaceRegistry.getQualifiedName(qualified_id.namespace_handle());
						size_t first_colon = ns_full_name.find("::");
						if (first_colon != std::string_view::npos &&
							first_colon + 2 < ns_full_name.size()) {
							std::string_view member_chain = ns_full_name.substr(first_colon + 2);
							parsed_member_chain = context.parser->buildQualifiedTypeMemberChain(member_chain);
							has_member_chain = !parsed_member_chain.empty();
						}
					}
					if (has_member_chain) {
						const StructStaticMember* previous_static_member = static_member;
						auto try_resolve_owner = [&](StringHandle owner_handle) {
							if (!owner_handle.isValid()) {
								return;
							}
							const TypeInfo* resolved_owner =
								context.parser->resolveBaseClassMemberTypeChain(
									StringTable::getStringView(owner_handle),
									parsed_member_chain);
							if (resolved_owner == nullptr ||
								resolved_owner->getStructInfo() == nullptr ||
								!resolved_owner->name().isValid() ||
								resolved_owner->name() == struct_handle) {
								return;
							}
							auto [resolved_member, resolved_member_owner] =
								resolved_owner->getStructInfo()->findStaticMemberRecursive(member_handle);
							if (resolved_member != nullptr) {
								static_member = resolved_member;
								owner_struct = resolved_member_owner;
								struct_info = resolved_owner->getStructInfo();
								resolved_type_info = resolved_owner;
								struct_handle = resolved_owner->name();
							}
						};
						if (context.struct_info != nullptr && context.struct_info->name.isValid()) {
							try_resolve_owner(context.struct_info->name);
						}
						try_resolve_owner(struct_handle);
						if (dependent_owner_handle.isValid()) {
							try_resolve_owner(dependent_owner_handle);
						}
						if (context.struct_info != nullptr) {
							for (const auto& base_class : context.struct_info->base_classes) {
								if (!base_class.type_index.is_valid()) {
									continue;
								}
								if (const TypeInfo* base_type_info = tryGetTypeInfo(base_class.type_index);
									base_type_info != nullptr &&
									base_type_info->name().isValid()) {
									try_resolve_owner(base_type_info->name());
								}
							}
							if (struct_info == context.struct_info &&
								static_member == previous_static_member &&
								owner_struct == struct_info) {
								static_member = nullptr;
								owner_struct = nullptr;
							}
						}
					}
				}

				// If member not found and there are base classes without struct info (ShapeOnly
				// instantiations), force-materialize them and retry the lookup.
				// This handles cases like is_enum<T>::value where T's base integral_constant<bool,V>
				// was only instantiated in ShapeOnly mode and has no recorded static members.
				if (!static_member && context.parser != nullptr && !struct_info->base_classes.empty()) {
					struct BaseToMaterialize {
						std::string_view template_name;
						std::vector<TemplateTypeArg> args;
					};
					std::vector<BaseToMaterialize> bases_to_materialize;
					bases_to_materialize.reserve(struct_info->base_classes.size());
					for (const auto& base : struct_info->base_classes) {
						if (!base.type_index.is_valid() || base.type_index.index() >= getTypeInfoCount()) {
							continue;
						}
						const TypeInfo& base_type_info = getTypeInfo(base.type_index);
						if (base_type_info.getStructInfo() != nullptr) {
							continue;
						}
						if (!base_type_info.isTemplateInstantiation()) {
							continue;
						}
						std::string_view base_template_name = StringTable::getStringView(base_type_info.baseTemplateName());
						if (base_template_name.empty()) {
							continue;
						}
						std::vector<TemplateTypeArg> base_args;
						base_args.reserve(base_type_info.templateArgs().size());
						for (const auto& arg_info : base_type_info.templateArgs()) {
							base_args.push_back(toTemplateTypeArg(arg_info));
						}
						bases_to_materialize.push_back({base_template_name, std::move(base_args)});
					}

					bool any_materialized = false;
					for (const auto& base_to_materialize : bases_to_materialize) {
						FLASH_LOG(ConstExpr, Debug, "Force-materializing ShapeOnly base '", base_to_materialize.template_name, "' to find member '", StringTable::getStringView(member_handle), "'");
						context.parser->materializeTemplateInstantiationForLookup(base_to_materialize.template_name, base_to_materialize.args);
						any_materialized = true;
					}
					if (any_materialized) {
						const TypeInfo* refreshed_type_info = nullptr;
						if (pre_materialization_type_index.is_valid()) {
							refreshed_type_info = tryGetTypeInfo(pre_materialization_type_index);
						}
						if (refreshed_type_info == nullptr && struct_handle.isValid()) {
							auto refreshed_type_it = getTypesByNameMap().find(struct_handle);
							if (refreshed_type_it != getTypesByNameMap().end() &&
								refreshed_type_it->second != nullptr) {
								refreshed_type_info = refreshed_type_it->second;
							}
						}
						if (refreshed_type_info != nullptr && refreshed_type_info->isStruct()) {
							resolved_type_info = refreshed_type_info;
							struct_info = refreshed_type_info->getStructInfo();
						}
						if (struct_info != nullptr) {
							auto [retry_member, retry_owner] = struct_info->findStaticMemberRecursive(member_handle);
							static_member = retry_member;
							owner_struct = retry_owner;
						} else {
							static_member = nullptr;
							owner_struct = nullptr;
						}
					}
				}

				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Static member found: ", (static_member != nullptr),
							  ", owner: ", (owner_struct != nullptr));
				}

				for (TypeIndex nested_enum_index : struct_info->getNestedEnumIndices()) {
					const TypeInfo* nested_enum_type = tryGetTypeInfo(nested_enum_index);
					const EnumTypeInfo* nested_enum_info = nested_enum_type ? nested_enum_type->getEnumInfo() : nullptr;
					if (!nested_enum_info) {
						continue;
					}
					if (auto enum_result = tryEvaluateEnumConstant(*nested_enum_info, nested_enum_index, member_handle)) {
						return *enum_result;
					}
				}

				auto evaluate_specialization_static_member = [&](StringHandle lookup_member_handle) -> std::optional<EvalResult> {
					if (!resolved_type_info || !resolved_type_info->isTemplateInstantiation()) {
						return std::nullopt;
					}
					std::string_view base_template_name = StringTable::getStringView(resolved_type_info->baseTemplateName());
					if (base_template_name.empty()) {
						return std::nullopt;
					}

					std::vector<TemplateTypeArg> exact_args;
					exact_args.reserve(resolved_type_info->templateArgs().size());
					for (const auto& arg_info : resolved_type_info->templateArgs()) {
						exact_args.push_back(toTemplateTypeArg(arg_info));
					}

					auto specialization_ast = gTemplateRegistry.lookupExactSpecialization(base_template_name, exact_args);
					if (!specialization_ast.has_value()) {
						specialization_ast = gTemplateRegistry.matchSpecializationPattern(base_template_name, exact_args);
					}
					if (!specialization_ast.has_value() || !specialization_ast->is<StructDeclarationNode>()) {
						return std::nullopt;
					}

					const auto& specialization_struct = specialization_ast->as<StructDeclarationNode>();
					for (const auto& static_member_decl : specialization_struct.static_members()) {
						if (static_member_decl.name != lookup_member_handle ||
							!static_member_decl.initializer.has_value()) {
							continue;
						}
						return evaluate(*static_member_decl.initializer, context);
					}

					return std::nullopt;
				};

				// Prefer an exact specialization's static-member initializer if normal
				// recursive lookup did not produce one. This lets standard library
				// patterns like `template<> struct trait<int> { static constexpr V value = ...; };`
				// override the primary template's inherited member regardless of which
				// alias-shaped argument reached the original template-instantiation cache.
				if (!static_member) {
					if (std::optional<EvalResult> variable_template_value =
							tryInstantiateQualifiedMemberVariableTemplate(
								resolved_type_info != nullptr
									? resolved_type_info
									: struct_type_it != getTypesByNameMap().end()
										? struct_type_it->second
										: nullptr);
						variable_template_value.has_value()) {
						return *variable_template_value;
					}
				}

				if (!static_member) {
					if (auto exact_value = evaluate_specialization_static_member(member_handle)) {
						FLASH_LOG(ConstExpr, Debug, "Evaluated static member from exact specialization AST");
						return *exact_value;
					}
				}

				if (!static_member && qualified_id.name() != kNestedTypeAliasName) {
					StringHandle nested_type_alias_handle{};
					auto nested_type_alias_it = getTypesByNameMap().end();
					auto try_nested_alias_lookup_for_owner = [&](StringHandle owner_name) -> bool {
						nested_type_alias_handle = StringTable::getOrInternStringHandle(
							StringBuilder()
								.append(owner_name)
								.append("::")
								.append(kNestedTypeAliasName)
								.commit());
						nested_type_alias_it = getTypesByNameMap().find(nested_type_alias_handle);
						return nested_type_alias_it != getTypesByNameMap().end() &&
							nested_type_alias_it->second != nullptr;
					};

					bool has_nested_alias = false;
					if (resolved_type_info != nullptr &&
						resolved_type_info->name().isValid() &&
						resolved_type_info->name() != struct_handle) {
						has_nested_alias = try_nested_alias_lookup_for_owner(resolved_type_info->name());
					}
					if (!has_nested_alias) {
						has_nested_alias = try_nested_alias_lookup_for_owner(struct_handle);
					}

					if (has_nested_alias) {
						const TypeInfo* nested_alias_info = nested_type_alias_it->second;
						ResolvedAliasTypeInfo resolved_nested_alias = resolveAliasTypeInfo(
							nested_alias_info->registeredTypeIndex().withCategory(
								nested_alias_info->typeEnum()));
						const TypeInfo* nested_target_info = resolved_nested_alias.terminal_type_info;
						if (!nested_target_info && resolved_nested_alias.type_index.is_valid()) {
							nested_target_info = tryGetTypeInfo(resolved_nested_alias.type_index);
						}
						if (nested_target_info != nullptr &&
							nested_target_info->isTemplateInstantiation() &&
							nested_target_info->getStructInfo() == nullptr &&
							context.parser != nullptr) {
							Parser& parser = *context.parser;
							const TypeInfo* nested_lookup_owner = nested_target_info;
							if (resolved_type_info != nullptr &&
								resolved_type_info->hasInstantiationContext()) {
								nested_lookup_owner = resolved_type_info;
							}
							auto materialize_nested_lookup_arg =
								[&](auto&& self,
									const TemplateTypeArg& source_arg,
									const TypeInfo* source_owner,
									int depth) -> TemplateTypeArg {
								constexpr int kMaxNestedLookupArgDepth = 8;
								TemplateTypeArg concrete_arg = source_arg;
								if (std::optional<TemplateTypeArg> rebound_arg =
										trySubstituteDependentTemplateArgForLookup(
											concrete_arg,
											context,
											source_owner,
											depth);
									rebound_arg.has_value()) {
									concrete_arg = std::move(*rebound_arg);
								}
								if (depth >= kMaxNestedLookupArgDepth ||
									concrete_arg.is_value ||
									!concrete_arg.type_index.is_valid()) {
									return concrete_arg;
								}
								const TypeInfo* concrete_type_info = tryGetTypeInfo(concrete_arg.type_index);
								if (concrete_type_info == nullptr ||
									!concrete_type_info->isTemplateInstantiation()) {
									return concrete_arg;
								}
								std::string_view concrete_base_name =
									StringTable::getStringView(concrete_type_info->baseTemplateName());
								if (concrete_base_name.empty()) {
									return concrete_arg;
								}
								std::vector<TemplateTypeArg> concrete_nested_args;
								concrete_nested_args.reserve(concrete_type_info->templateArgs().size());
								for (const auto& concrete_nested_arg_info : concrete_type_info->templateArgs()) {
									TemplateTypeArg concrete_nested_arg = self(
										self,
										toTemplateTypeArg(concrete_nested_arg_info),
										concrete_type_info,
										depth + 1);
									concrete_nested_args.push_back(std::move(concrete_nested_arg));
								}
								Parser::AliasTemplateMaterializationResult concrete_materialized =
									parser.materializeTemplateInstantiationForLookup(
										concrete_base_name,
										concrete_nested_args);
								const TypeInfo* concrete_resolved_info = concrete_materialized.resolved_type_info;
								if (concrete_resolved_info == nullptr &&
									!concrete_materialized.instantiated_name.empty()) {
									concrete_resolved_info = findTypeByName(
										StringTable::getOrInternStringHandle(
											concrete_materialized.instantiated_name));
								}
								if (concrete_resolved_info == nullptr) {
									return concrete_arg;
								}
								TemplateTypeArg resolved_type_arg;
								resolved_type_arg.type_index =
									concrete_resolved_info->registeredTypeIndex().withCategory(
										concrete_resolved_info->typeEnum());
								resolved_type_arg.setCategory(concrete_resolved_info->typeEnum());
								return rebindDependentTemplateTypeArg(resolved_type_arg, concrete_arg);
							};
							std::vector<TemplateTypeArg> nested_template_args;
							nested_template_args.reserve(nested_target_info->templateArgs().size());
							for (const auto& arg_info : nested_target_info->templateArgs()) {
								TemplateTypeArg nested_arg = materialize_nested_lookup_arg(
									materialize_nested_lookup_arg,
									toTemplateTypeArg(arg_info),
									nested_lookup_owner,
									0);
								nested_template_args.push_back(std::move(nested_arg));
							}
							std::string_view nested_base_name =
								StringTable::getStringView(nested_target_info->baseTemplateName());
							if (!nested_base_name.empty()) {
								Parser::AliasTemplateMaterializationResult materialized_target =
									parser.materializeTemplateInstantiationForLookup(
										nested_base_name,
										nested_template_args);
								if (materialized_target.resolved_type_info != nullptr) {
									nested_target_info = materialized_target.resolved_type_info;
								} else if (!materialized_target.instantiated_name.empty()) {
									nested_target_info = findTypeByName(
										StringTable::getOrInternStringHandle(
											materialized_target.instantiated_name));
								}
							}
						}
						if (nested_target_info != nullptr &&
							nested_target_info->name() != struct_handle) {
							NamespaceHandle nested_target_ns = gNamespaceRegistry.getOrCreateNamespace(
								NamespaceRegistry::GLOBAL_NAMESPACE,
								nested_target_info->name());
							QualifiedIdentifierNode& rebound_qual_id =
								gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
									nested_target_ns,
									qualified_id.identifier_token());
							ExpressionNode& rebound_expr =
								gChunkedAnyStorage.emplace_back<ExpressionNode>(rebound_qual_id);
							FLASH_LOG(ConstExpr, Debug, "Rebound missing static member through nested type alias");
							return evaluate(ASTNode(&rebound_expr), context);
						}
					}
				}

				if (static_member && owner_struct) {
					FLASH_LOG(ConstExpr, Debug, "Static member is_const: ", static_member->is_const(),
							  ", has_initializer: ", static_member->initializer.has_value());

					// Always try to trigger lazy instantiation for static members.
					// The member might have an initializer from template parsing that
					// still contains unsubstituted template parameters (like _R1::num).
					// The lazy system will substitute them and update the initializer.
					// If the member is not in the lazy registry, this is a fast no-op.
					if (context.parser != nullptr) {
						bool did_lazy = context
											.requireParserOwnedSema("qualified static member materialization")
											.parserSemanticServices()
											.tryInstantiateLazyStaticMember(
												owner_struct->name,
												member_handle);
						if (did_lazy) {
							context.normalizePendingSemanticRoots();
							// Re-lookup the static member after instantiation
							auto relookup_result = struct_info->findStaticMemberRecursive(member_handle);
							if (relookup_result.first && relookup_result.first->initializer.has_value()) {
								FLASH_LOG(ConstExpr, Debug, "After lazy instantiation, evaluating initializer");
								return evaluate(relookup_result.first->initializer.value(), context);
							}
						}
					}

					// Found a static member - evaluate its initializer if available
					// Note: Even if not marked const, we can evaluate constexpr initializers
					if (static_member->initializer.has_value()) {
						FLASH_LOG(ConstExpr, Debug, "Evaluating static member initializer");
					}
					if (!static_member->initializer.has_value()) {
						if (auto exact_value = evaluate_specialization_static_member(member_handle)) {
							FLASH_LOG(ConstExpr, Debug, "Evaluated static member from specialization AST");
							return *exact_value;
						}
					}

					if (gEvaluatingStaticMembers.contains(static_member) &&
						context.parser != nullptr) {
						std::vector<QualifiedTypeMemberAccess> owner_chain;
						std::vector<std::vector<TemplateTypeArg>> owner_chain_template_arg_storage;
						StringHandle dependent_owner_handle;
						if (const auto* dependent_record = qualified_id.dependentQualifiedName();
							dependent_record != nullptr &&
							dependent_record->member_chain.size() > 1) {
							dependent_owner_handle = dependent_record->owner_name;
							owner_chain.reserve(dependent_record->member_chain.size() - 1);
							owner_chain_template_arg_storage.resize(dependent_record->member_chain.size() - 1);
							for (size_t member_index = 0;
								 member_index + 1 < dependent_record->member_chain.size();
								 ++member_index) {
								const auto& member_record = dependent_record->member_chain[member_index];
								QualifiedTypeMemberAccess member_access;
								member_access.member_name = member_record.name;
								member_access.has_template_arguments = false;
								if (member_record.has_template_arguments) {
									auto& arg_storage = owner_chain_template_arg_storage[member_index];
									arg_storage.reserve(member_record.template_arguments.size());
									for (const TypeInfo::TemplateArgInfo& stored_arg : member_record.template_arguments) {
										TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
										if (std::optional<TemplateTypeArg> rebound_arg =
												trySubstituteDependentTemplateArgForLookup(
													arg,
													context,
													nullptr,
													0);
											rebound_arg.has_value()) {
											arg = std::move(*rebound_arg);
										}
										if (arg.is_value && arg.dependent_expr.has_value()) {
											EvalResult evaluated_arg = evaluate(*arg.dependent_expr, context);
											if (!evaluated_arg.success()) {
												arg_storage.clear();
												break;
											}
											arg = templateTypeArgFromEvalResult(evaluated_arg);
										}
										if (arg.is_dependent) {
											arg_storage.clear();
											break;
										}
										arg_storage.push_back(std::move(arg));
									}
									if (!arg_storage.empty()) {
										member_access.has_template_arguments = true;
										member_access.template_arguments = &arg_storage;
									}
								}
								owner_chain.push_back(std::move(member_access));
							}
						}
						if (owner_chain.empty()) {
							std::string_view ns_full_name =
								gNamespaceRegistry.getQualifiedName(qualified_id.namespace_handle());
							size_t first_colon = ns_full_name.find("::");
							if (first_colon != std::string_view::npos &&
								first_colon + 2 < ns_full_name.size()) {
								std::string_view member_chain = ns_full_name.substr(first_colon + 2);
								owner_chain = context.parser->buildQualifiedTypeMemberChain(member_chain);
							}
						}
						if (!owner_chain.empty()) {
							auto try_resolve_alternate_owner =
								[&](StringHandle owner_handle) -> std::optional<EvalResult> {
								if (!owner_handle.isValid()) {
									return std::nullopt;
								}
								const TypeInfo* resolved_owner =
									context.parser->resolveBaseClassMemberTypeChain(
										StringTable::getStringView(owner_handle),
										owner_chain);
								if (resolved_owner == nullptr ||
									resolved_owner->getStructInfo() == nullptr) {
									return std::nullopt;
								}
								auto [alternate_member, alternate_owner] =
									resolved_owner->getStructInfo()->findStaticMemberRecursive(member_handle);
								if (alternate_member == nullptr ||
									alternate_member == static_member ||
									alternate_owner == nullptr) {
									return std::nullopt;
								}
								return tryReadStaticMemberConstant(*alternate_member, context);
							};
							InlineVector<StringHandle, 6> owner_candidates;
							if (context.struct_info != nullptr && context.struct_info->name.isValid()) {
								owner_candidates.push_back(context.struct_info->name);
								for (const auto& base_class : context.struct_info->base_classes) {
									if (!base_class.type_index.is_valid()) {
										continue;
									}
									if (const TypeInfo* base_type_info = tryGetTypeInfo(base_class.type_index);
										base_type_info != nullptr && base_type_info->name().isValid()) {
										owner_candidates.push_back(base_type_info->name());
									}
								}
							}
							owner_candidates.push_back(struct_handle);
							if (dependent_owner_handle.isValid()) {
								owner_candidates.push_back(dependent_owner_handle);
							}
							for (StringHandle candidate_handle : owner_candidates) {
								if (!candidate_handle.isValid()) {
									continue;
								}
								if (std::optional<EvalResult> alternate_result =
										try_resolve_alternate_owner(candidate_handle);
									alternate_result.has_value()) {
									return *alternate_result;
								}
							}
						}
					}

					return tryReadStaticMemberConstant(*static_member, context);
				}
			}
		}

		if (qualifier_is_known_namespace) {
			return EvalResult::error("Undefined qualified identifier in constant expression: " + qualified_id.full_name());
		}

		// Not found in symbol table or as struct static member
		// Check if this looks like a template instantiation with dependent arguments
		// Pattern: __template_name__DependentArg::member
		// Work with string_view to avoid unnecessary copies
		std::string_view ns_name = gNamespaceRegistry.getQualifiedName(qualified_id.namespace_handle());
		std::string_view member_name = qualified_id.name();

		// Check if the namespace part looks like a template instantiation (contains template argument separator)
		// Template names with arguments get mangled as template_name_arg1_arg2...
		// If any argument is a template parameter (starts with _ often), it's template-dependent
		// BUT: Don't treat names like "is_integral_int" as dependent - "int" is a concrete type!
		// Only treat as dependent if it contains identifiers that START with underscore (like _Tp, _Up)
		// or have double underscores (like __type_parameter)
		bool looks_dependent = false;
		if (!ns_name.empty() && ns_name.find('_') != std::string_view::npos) {
			// Check if any component looks like a template parameter (starts with _ or __)
			// Split by underscore and check each part
			for (size_t i = 0; i < ns_name.size();) {
				if (ns_name[i] == '_') {
					// Found underscore - check if next char is also underscore or uppercase
					// Template parameters often look like: _Tp, _Up, _T, __type, etc.
					if (i + 1 < ns_name.size()) {
						char next = ns_name[i + 1];
						if (next == '_' || std::isupper(static_cast<unsigned char>(next))) {
							looks_dependent = true;
							break;
						}
					}
				}
				++i;
			}
		}

		if (looks_dependent && context.parser != nullptr) {
			// This might be a template instantiation with dependent arguments
			// Treat it as template-dependent
			return EvalResult::error("Template instantiation with dependent arguments in constant expression: " +
										 std::string(ns_name) + "::" + std::string(member_name),
									 EvalErrorType::TemplateDependentExpression);
		}

		// Not found in symbol table or as struct static member
		return EvalResult::error("Undefined qualified identifier in constant expression: " + qualified_id.full_name());
	}

	const ASTNode& symbol_node = *symbol_opt;

	// Check if it's a variable declaration (constexpr)
	if (symbol_node.is<VariableDeclarationNode>()) {
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Qualified variable must be constexpr: " + qualified_id.full_name());
		}
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + qualified_id.full_name());
		}
		if (initializer->is<InitializerListNode>()) {
			const TypeSpecifierNode& type_spec = var_decl.declaration().type_specifier_node();
			if (type_spec.array_dimension_count() > 0) {
				return materializeArrayInitializer(
					type_spec.type_index(),
					type_spec.array_dimensions(),
					initializer->as<InitializerListNode>(),
					context);
			}
		}

		return evaluate(initializer.value(), context);
	}

	if (symbol_node.is<DeclarationNode>()) {
		const DeclarationNode& decl = symbol_node.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_specifier_node();
		if (type_spec.category() == TypeCategory::Enum) {
			if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
				if (const EnumTypeInfo* enum_info = type_info->getEnumInfo()) {
					if (auto enum_result = tryEvaluateEnumConstant(*enum_info, type_spec.type_index(), qualified_id.nameHandle())) {
						return *enum_result;
					}
				}
			}
			return EvalResult::error("Enum constant value not found: " + qualified_id.full_name());
		}
	}

	// Could be other types like non-enum constants - add support as needed
	return EvalResult::error("Qualified identifier is not a constant expression: " + qualified_id.full_name());
}

// Shared helper: resolve the pointed-to constexpr variable named by pointed_name, then
// extract the member named member_name from it.
// When check_static is true, also handles access to static struct members (used by the
// top-level evaluate_member_access path which supports static-through-instance access).
EvalResult Evaluator::evaluate_arrow_member_from_pointer_var(
	std::string_view pointed_name, std::string_view member_name,
	EvaluationContext& context, bool check_static) {

	ResolvedConstexprObject resolved_ptr_obj;
	if (auto resolve_error = resolve_constexpr_object_source(
			nullptr, pointed_name, context, "arrow member access", resolved_ptr_obj)) {
		return *resolve_error;
	}
	if (!resolved_ptr_obj.initializer) {
		return EvalResult::error("Arrow member access (->): could not resolve pointed-to object '" + std::string(pointed_name) + "'");
	}

	const VariableDeclarationNode* ptr_var_decl = resolved_ptr_obj.var_decl;
	TypeIndex ptr_type_index = resolved_ptr_obj.declared_type_index;
	if (ptr_var_decl) {
		const ASTNode& var_type_node = ptr_var_decl->declaration().type_node();
		if (var_type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& var_type_spec = var_type_node.as<TypeSpecifierNode>();
			ptr_type_index = var_type_spec.type_index();

			if (check_static) {
				const TypeInfo* type_info = tryGetTypeInfo(ptr_type_index);
				const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
				if (struct_info) {
					StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
					if (static_member && owner_struct) {
						return evaluate_static_member_initializer_or_default(*static_member, context);
					}
				}
			}
		}
		if (!ptr_var_decl->is_constexpr()) {
			return EvalResult::error("Arrow member access (->): pointed-to variable must be constexpr: " + std::string(pointed_name));
		}
	}

	ResolvedConstexprMemberSource resolved_member;
	if (auto member_error = resolve_constexpr_member_source_from_initializer(
			*resolved_ptr_obj.initializer, ptr_type_index, member_name,
			"arrow member access", context, resolved_member)) {
		return *member_error;
	}
	if (resolved_member.value.has_value())
		return resolved_member.value.value();
	if (!resolved_member.initializer.has_value()) {
		return EvalResult::error("Arrow member access (->): internal error resolving member '" + std::string(member_name) + "'");
	}
	if (!resolved_member.evaluation_bindings.empty()) {
		return evaluate_expression_with_bindings(
			resolved_member.initializer.value(), resolved_member.evaluation_bindings, context);
	}
	return evaluate(resolved_member.initializer.value(), context);
}

// Evaluate member access (e.g., obj.member or struct_type::static_member)
// Also supports nested member access (e.g., obj.inner.value)
EvalResult Evaluator::evaluate_member_access(const MemberAccessNode& member_access, EvaluationContext& context) {
	// Get the object expression (e.g., 'p1' in 'p1.x')
	const ASTNode& object_expr = member_access.object();
	std::string_view member_name = member_access.member_name();

	// Handle arrow access (ptr->member): evaluate the pointer expression to get the
	// pointed-to variable name, then perform member access on that variable.
	if (member_access.is_arrow()) {
		auto ptr_result = evaluate(object_expr, context);
		if (!ptr_result.success())
			return ptr_result;
		if (!ptr_result.pointer_to_var.isValid()) {
			return EvalResult::error("Arrow member access (->): object must be a constexpr pointer in constant expressions");
		}
		std::string_view pointed_name = StringTable::getStringView(ptr_result.pointer_to_var);
		// Try to dereference through binding-aware path first so pointer snapshots
		// (e.g. synthesized std::initializer_list backing arrays) remain usable even
		// when the original synthetic binding no longer exists in the active scope.
		const auto* active_bindings = context.local_bindings;
		auto elem_result = deref_pointer_with_bindings(
			ptr_result,
			active_bindings ? *active_bindings : emptyEvalBindings(),
			context);
		if (!elem_result.success()) {
			if (!isRecoverablePointerDerefFailure(elem_result)) {
				return elem_result;
			}
		} else {
			auto it = elem_result.object_member_bindings.find(member_name);
			if (it != elem_result.object_member_bindings.end()) {
				return it->second;
			}
		}
		// For a plain (non-array) struct pointer at offset 0, fall back to the standard resolver
		// which handles constructor-initializer-list member extraction.
		if (ptr_result.pointer_offset == 0) {
			return evaluate_arrow_member_from_pointer_var(pointed_name, member_name, context, /*check_static=*/true);
		}
		return EvalResult::error("Arrow member access (->): member '" + std::string(member_name) +
								 "' not found on array element at offset " + std::to_string(ptr_result.pointer_offset));
	}

	// Check if this is a nested member access (e.g., obj.inner.value)
	if (object_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
		if (const auto* member_access_ptr = std::get_if<MemberAccessNode>(&expr_node)) {
			// Nested member access - first get the intermediate struct initializer
			const MemberAccessNode& inner_access = *member_access_ptr;
			return evaluate_nested_member_access(inner_access, member_name, context);
		}
	}

	// For constexpr struct member access, we need to handle the case where:
	// - The object is an identifier referencing a constexpr variable
	// - The variable is initialized with a ConstructorCallNode
	// - We need to find the constructor declaration and its member initializer list
	// - Extract the member value from the initializer expression

	const IdentifierNode* object_identifier = tryGetIdentifier(object_expr);

	std::string_view var_name = getIdentifierNameFromAstNode(object_expr);
	if (var_name.empty()) {
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			auto try_resolve_evaluated_object_member =
				[&](const ASTNode& evaluated_object_expr) -> std::optional<EvalResult> {
				EvalResult object_result = evaluate(evaluated_object_expr, context);
				if (!object_result.success()) {
					return object_result;
				}

				auto member_it = object_result.object_member_bindings.find(member_name);
				if (member_it != object_result.object_member_bindings.end()) {
					return member_it->second;
				}

				if (object_result.pointer_to_var.isValid()) {
					EvalResult deref_result = dereference_constexpr_pointer(
						StringTable::getStringView(object_result.pointer_to_var),
						context,
						object_result.pointer_offset);
					if (!deref_result.success()) {
						return deref_result;
					}

					auto deref_member_it = deref_result.object_member_bindings.find(member_name);
					if (deref_member_it != deref_result.object_member_bindings.end()) {
						return deref_member_it->second;
					}
				}

				return std::nullopt;
			};
			// Check for ArraySubscriptNode
			if (const auto* array_subscript = std::get_if<ArraySubscriptNode>(&expr_node)) {
				if (auto evaluated_member = try_resolve_evaluated_object_member(object_expr)) {
					return *evaluated_member;
				}
				// Array subscript on struct - evaluate array element then access member
				return evaluate_array_subscript_member_access(*array_subscript, member_name, context);
			}
			if (const auto* call_expr = std::get_if<CallExprNode>(&expr_node)) {
				if (auto evaluated_member = try_resolve_evaluated_object_member(object_expr)) {
					return *evaluated_member;
				}
				return evaluate_function_call_member_access(*call_expr, member_name, context);
			}
		}
		return EvalResult::error("Complex member access expressions not yet supported in constant expressions");
	}

	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
			object_identifier,
			var_name,
			context,
			"member access",
			resolved_object)) {
		return *resolve_error;
	}

 // Phase C: if the object was materialized from pre-packed constant bytes,
 // try to extract the member value from object_member_bindings.
 // Array/struct members are excluded from the materialized bindings, so
 // a miss here is expected and we fall through to normal AST evaluation.
	if (resolved_object.materialized_value.has_value()) {
		const EvalResult& mat = *resolved_object.materialized_value;
		auto it = mat.object_member_bindings.find(member_name);
		if (it != mat.object_member_bindings.end()) {
			return it->second;
		}
 // Not found in materialized bindings (e.g., array member) — fall through
 // to normal AST-based evaluation below.
	}

	if (resolved_object.initializer == nullptr) {
		return EvalResult::error("Internal error: unresolved member access object source");
	}

	const VariableDeclarationNode* var_decl = resolved_object.var_decl;
	TypeIndex var_type_index = resolved_object.declared_type_index;

	// Before checking if the variable is constexpr, check if we're accessing a static member
	// Static members can be accessed through any instance (even non-constexpr)
	// because they don't depend on the instance

	if (var_decl) {
		const DeclarationNode& var_declaration = var_decl->declaration();
		const ASTNode& var_type_node = var_declaration.type_node();
		if (var_type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& var_type_spec = var_type_node.as<TypeSpecifierNode>();
			var_type_index = var_type_spec.type_index();

			if (const TypeInfo* var_type_info = tryGetTypeInfo(var_type_index)) {
				const StructTypeInfo* struct_info = var_type_info->getStructInfo();

				if (struct_info) {
					// Look for static member
					StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);

					if (static_member && owner_struct) {
						FLASH_LOG(ConstExpr, Debug, "Accessing static member through instance: ", member_name);

						// Found a static member - evaluate its initializer if available
						return evaluate_static_member_initializer_or_default(*static_member, context);
					}
				}
			}
		}

		// If not a static member access, check if it's a constexpr variable
		if (!var_decl->is_constexpr()) {
			return EvalResult::error("Variable in member access must be constexpr: " + std::string(var_name));
		}
	}

	ResolvedConstexprMemberSource resolved_member;
	if (auto member_error = resolve_constexpr_member_source_from_initializer(
			*resolved_object.initializer,
			var_type_index,
			member_name,
			"member access",
			context,
			resolved_member)) {
		return *member_error;
	}

	if (resolved_member.value.has_value()) {
		return resolved_member.value.value();
	}

	if (!resolved_member.initializer.has_value()) {
		return EvalResult::error("Internal error: unresolved member source in member access");
	}

	if (resolved_member.member_info &&
		is_struct_type(resolved_member.member_info->type_index.category())) {
		const TypeIndex member_type_index = resolved_member.member_info->type_index;
		const TypeInfo* member_type_info = tryGetTypeInfo(member_type_index);
		const StructTypeInfo* member_struct_info = member_type_info ? member_type_info->getStructInfo() : nullptr;
		if (member_struct_info) {
			const ASTNode& member_initializer = resolved_member.initializer.value();
			const auto* member_bindings = resolved_member.evaluation_bindings.empty()
											 ? nullptr
											 : &resolved_member.evaluation_bindings;

			if (member_initializer.is<InitializerListNode>()) {
				return materialize_aggregate_object_value(
					member_struct_info,
					member_type_index,
					member_initializer.as<InitializerListNode>(),
					context,
					member_bindings);
			}

			if (const ConstructorCallNode* ctor_call = extract_constructor_call(resolved_member.initializer)) {
				return materialize_constructor_object_value(*ctor_call, context, member_bindings);
			}

			EvalResult init_arg_result = member_bindings
											 ? evaluate_expression_with_bindings_const(member_initializer, *member_bindings, context)
											 : evaluate(member_initializer, context);
			if (!init_arg_result.success()) {
				return init_arg_result;
			}
			if (init_arg_result.object_type_index.is_valid() || !init_arg_result.object_member_bindings.empty()) {
				return init_arg_result;
			}

			auto ctor_resolution = resolve_constructor_overload_arity(*member_struct_info, 1, true);
			const ConstructorDeclarationNode* matching_ctor = ctor_resolution.selected_overload;
			if (matching_ctor) {
				std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
				std::vector<EvalResult> ctor_args;
				ctor_args.push_back(std::move(init_arg_result));
				auto bind_result = bind_pre_evaluated_arguments(
					matching_ctor->parameter_nodes(),
					ctor_args,
					ctor_param_bindings,
					context,
					"Failed to bind constructor parameter while materializing struct-valued member access",
					true);
				if (!bind_result.success()) {
					return bind_result;
				}

				EvalResult object_result = EvalResult::from_int(0LL);
				object_result.object_type_index = member_type_index;
				auto materialize_result = materialize_members_from_constructor(
					member_struct_info,
					*matching_ctor,
					ctor_param_bindings,
					object_result.object_member_bindings,
					context,
					false);
				if (!materialize_result.success()) {
					return materialize_result;
				}
				return object_result;
			}
		}
	}

	if (!resolved_member.evaluation_bindings.empty()) {
		return evaluate_expression_with_bindings(
			resolved_member.initializer.value(),
			resolved_member.evaluation_bindings,
			context);
	}

	return evaluate(resolved_member.initializer.value(), context);
}

std::optional<EvalResult> Evaluator::resolve_constexpr_member_source_from_initializer(
	const std::optional<ASTNode>& object_initializer,
	TypeIndex declared_type_index,
	std::string_view member_name,
	std::string_view usage_name,
	EvaluationContext& context,
	ResolvedConstexprMemberSource& resolved_member,
	const std::unordered_map<std::string_view, EvalResult>* enclosing_bindings) {
	resolved_member = {};

	if (!object_initializer.has_value()) {
		return EvalResult::error("Constexpr " + std::string(usage_name) + " object has no initializer");
	}

	StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
	auto find_member_info = [member_name_handle](const StructTypeInfo* struct_info) -> const StructMember* {
		return findMemberInfoRecursive(struct_info, member_name_handle);
	};

	auto resolve_explicit_member = [&](const StructTypeInfo* struct_info, const ASTNode* explicit_initializer) -> std::optional<EvalResult> {
		const StructMember* member_info = find_member_info(struct_info);
		if (!member_info) {
			return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name));
		}

		resolved_member.member_info = member_info;
		if (explicit_initializer) {
			resolved_member.initializer = *explicit_initializer;
			if (enclosing_bindings) {
				resolved_member.evaluation_bindings = *enclosing_bindings;
			}
			return std::nullopt;
		}

		if (member_info->default_initializer.has_value()) {
			resolved_member.initializer = member_info->default_initializer.value();
			resolved_member.evaluation_bindings.clear();
			return std::nullopt;
		}

		return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name) + " and has no default value");
	};

	const ASTNode& initializer = object_initializer.value();
	if (initializer.is<InitializerListNode>()) {
		const TypeInfo* declared_type_info = tryGetTypeInfo(declared_type_index);
		if (!declared_type_info) {
			return EvalResult::error("Invalid type index in " + std::string(usage_name));
		}

		const StructTypeInfo* struct_info = declared_type_info->getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Aggregate-initialized constexpr object is not a struct in " + std::string(usage_name));
		}

		const InitializerListNode& init_list = initializer.as<InitializerListNode>();
		size_t positional_member_index = 0;
		for (size_t init_index = 0; init_index < init_list.size(); ++init_index) {
			StringHandle current_member_name;
			if (init_list.is_designated(init_index)) {
				current_member_name = init_list.member_name(init_index);
			} else if (positional_member_index < struct_info->members.size()) {
				current_member_name = struct_info->members[positional_member_index].getName();
				positional_member_index++;
			} else {
				break;
			}

			if (current_member_name == member_name_handle) {
				return resolve_explicit_member(struct_info, &init_list.initializers()[init_index]);
			}
		}

		return resolve_explicit_member(struct_info, nullptr);
	}

	const ConstructorCallNode* ctor_call_ptr = extract_constructor_call(object_initializer);
	if (!ctor_call_ptr) {
		// Handle function-call initializers: constexpr Vec2 p = make_point(1, 2)
		// The initializer may be an ASTNode holding a CallExprNode directly,
		// or wrapped in an ExpressionNode.  Evaluate and use object_member_bindings.
		bool is_func_call = initializer.is<CallExprNode>();
		if (!is_func_call && initializer.is<ExpressionNode>()) {
			const ExpressionNode& expr = initializer.as<ExpressionNode>();
			is_func_call = std::holds_alternative<CallExprNode>(expr);
		}
		if (is_func_call) {
			auto func_result = evaluate(initializer, context);
			if (func_result.success() && !func_result.object_member_bindings.empty()) {
				auto it = func_result.object_member_bindings.find(member_name);
				if (it != func_result.object_member_bindings.end()) {
					resolved_member.value = it->second;
					return std::nullopt;
				}
				return EvalResult::error("Member '" + std::string(member_name) + "' not found in function call result");
			}
			if (!func_result.success()) {
				return func_result;
			}
		}
		// Fallback: evaluate the initializer directly.  This handles ternary operators,
		// parenthesised expressions, and any other expression that yields a struct value
		// (e.g., `constexpr Pt p = (true ? Pt{3,7} : Pt{1,2})`).
		{
			static const std::unordered_map<std::string_view, EvalResult> empty_bindings;
			const std::unordered_map<std::string_view, EvalResult>& eval_bindings =
				enclosing_bindings ? *enclosing_bindings : empty_bindings;
			EvalResult gen_result = evaluate_expression_with_bindings_const(initializer, eval_bindings, context);
			// Propagate evaluation failures rather than swallowing them with the generic
			// "requires a struct initializer" message below (e.g. a failed function call
			// inside a ternary initializer should surface its own diagnostic).
			if (!gen_result.success())
				return gen_result;
			if (!gen_result.object_member_bindings.empty()) {
				auto it = gen_result.object_member_bindings.find(member_name);
				if (it != gen_result.object_member_bindings.end()) {
					resolved_member.value = it->second;
					return std::nullopt;
				}
				return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name));
			}
		}
		return EvalResult::error("Constexpr " + std::string(usage_name) + " requires a struct initializer");
	}

	const ConstructorCallNode& ctor_call = *ctor_call_ptr;
	const TypeSpecifierNode& type_spec = ctor_call.type_node();
	if (!is_struct_type(type_spec.category())) {
		return EvalResult::error("Constexpr " + std::string(usage_name) + " requires a struct type");
	}

	TypeIndex type_index = type_spec.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	if (!type_info) {
		return EvalResult::error("Invalid type index in " + std::string(usage_name));
	}

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info) {
		return EvalResult::error("Type is not a struct in " + std::string(usage_name));
	}

	const auto& ctor_args = ctor_call.arguments();
	const ConstructorDeclarationNode* matching_ctor = ctor_call.resolved_constructor();
	if (!matching_ctor) {
		matching_ctor = find_matching_constructor(struct_info, ctor_args, context, false, nullptr);
	}
	if (!matching_ctor) {
		// No explicit constructor found. For aggregates without user-defined constructors,
		// try aggregate initialization directly with the constructor arguments.
		// Use Paren style because this came from paren-init (e.g., Type(arg1, arg2)),
		// not brace-init, so narrowing conversions should be allowed.
		if (!struct_info->hasUserDeclaredConstructor() && !ctor_args.empty()) {
			InitializerListNode init_list(InitializerListNode::InitializationStyle::Paren);
			for (size_t i = 0; i < ctor_args.size(); ++i) {
				init_list.add_initializer(ctor_args[i]);
			}
			EvalResult obj_result = EvalResult::from_int(0);
			obj_result.object_type_index = type_index;
			auto bind_result = bind_members_from_initializer_list(
				struct_info, init_list, obj_result.object_member_bindings, context, enclosing_bindings);
			if (!bind_result.success()) {
				return bind_result;
			}
			auto it = obj_result.object_member_bindings.find(member_name);
			if (it != obj_result.object_member_bindings.end()) {
				resolved_member.value = it->second;
				return std::nullopt;
			}
			return EvalResult::error("Member '" + std::string(member_name) + "' not found in aggregate initialization result");
		}
		return EvalResult::error("No matching constructor found for constexpr " + std::string(usage_name));
	}

	const StructMember* member_info = find_member_info(struct_info);
	if (!member_info) {
		return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name));
	}
	resolved_member.member_info = member_info;

	bool initializer_uses_ctor_bindings = false;
	for (const auto& mem_init : matching_ctor->member_initializers()) {
		if (mem_init.member_name == member_name) {
			resolved_member.initializer = mem_init.initializer_expr;
			initializer_uses_ctor_bindings = true;
			break;
		}
	}
	if (!resolved_member.initializer.has_value() && member_info->default_initializer.has_value()) {
		resolved_member.initializer = member_info->default_initializer.value();
	}

	const auto& params = matching_ctor->parameter_nodes();
	auto bind_result = bind_evaluated_arguments(
		params,
		ctor_args,
		resolved_member.evaluation_bindings,
		context,
		"Invalid parameter node in constexpr member source constructor",
		enclosing_bindings,
		true);
	if (!bind_result.success()) {
		return bind_result;
	}

	std::unordered_map<std::string_view, EvalResult> member_bindings;
	auto materialize_result = materialize_members_from_constructor(
		struct_info,
		*matching_ctor,
		resolved_member.evaluation_bindings,
		member_bindings,
		context,
		true);
	if (!materialize_result.success()) {
		return materialize_result;
	}

	auto member_it = member_bindings.find(member_name);
	if (member_it == member_bindings.end()) {
		return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name) + " and has no default value");
	}
	resolved_member.value = member_it->second;
	if (!initializer_uses_ctor_bindings) {
		resolved_member.evaluation_bindings.clear();
	}

	return std::nullopt;
}

EvalResult Evaluator::evaluate_pointer_to_member_access(const PointerToMemberAccessNode& member_access, EvaluationContext& context) {
	EvalResult member_pointer_result = evaluate(member_access.member_pointer(), context);
	if (!member_pointer_result.success()) {
		return member_pointer_result;
	}

	if (member_pointer_result.is_null_member_pointer) {
		return EvalResult::error(
			"Pointer-to-member access does not support null member pointers in constant expressions",
			EvalErrorType::NotConstantExpression);
	}

	if (!member_pointer_result.member_pointer_member.isValid()) {
		return EvalResult::error("Pointer-to-member access requires a constexpr member-object pointer");
	}

	Token member_token(
		Token::Type::Identifier,
		StringTable::getStringView(member_pointer_result.member_pointer_member),
		kSyntheticTokenLine,
		kSyntheticTokenColumn,
		kSyntheticTokenFileIndex);
	// Synthetic lookup token: only the interned identifier spelling matters here.
	return evaluate_member_access(
		MemberAccessNode(member_access.object(), member_token, member_access.is_arrow()),
		context);
}

// Helper to get StructTypeInfo from a TypeSpecifierNode
const StructTypeInfo* Evaluator::get_struct_info_from_type(const TypeSpecifierNode& type_spec) {
	if (!is_struct_type(type_spec.category())) {
		return nullptr;
	}

	TypeIndex type_index = type_spec.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	if (!type_info) {
		return nullptr;
	}

	return type_info->getStructInfo();
}

std::optional<EvalResult> Evaluator::resolve_constexpr_object_source(
	const IdentifierNode* object_identifier,
	std::string_view object_name,
	EvaluationContext& context,
	std::string_view usage_name,
	ResolvedConstexprObject& resolved_object) {
	resolved_object = {};

	if (object_name == "this" && context.struct_info && tryGetTypeInfo(context.struct_type_index)) {
		EvalResult this_obj = EvalResult::from_int(0);
		this_obj.object_type_index = context.struct_type_index;
		for (const auto& member : context.struct_info->members) {
			std::string_view member_name = StringTable::getStringView(member.getName());
			if (const EvalResult* binding = findLocalBinding(member_name, context)) {
				this_obj.object_member_bindings[member_name] = *binding;
			}
		}
		resolved_object.declared_type_index = context.struct_type_index;
		resolved_object.materialized_value = std::move(this_obj);
		return std::nullopt;
	}

	bool resolved_from_local_object = false;
	if (const EvalResult* local_object = findLocalBinding(object_name, context);
		local_object) {
		if (local_object->object_type_index.is_valid()) {
			resolved_object.declared_type_index = local_object->object_type_index;
			resolved_object.materialized_value = *local_object;
			resolved_from_local_object = true;
		} else if (local_object->pointer_to_var.isValid() && context.local_bindings) {
			EvalResult deref_result = deref_pointer_with_bindings(
				*local_object,
				*context.local_bindings,
				context);
			if (!deref_result.success()) {
				return deref_result;
			}
			if (deref_result.object_type_index.is_valid()) {
				resolved_object.declared_type_index = deref_result.object_type_index;
				resolved_object.materialized_value = std::move(deref_result);
				resolved_from_local_object = true;
			}
		}
	}

	// For local bound objects, still try to recover the original declaration
	// initializer as a fallback source (needed when nested members are not fully
	// materialized in the bound value).
	if (resolved_from_local_object) {
		if (context.symbols) {
			std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(object_identifier, object_name, *context.symbols);
			if (!symbol_opt.has_value() && context.global_symbols && context.global_symbols != context.symbols) {
				symbol_opt = lookup_identifier_symbol(object_identifier, object_name, *context.global_symbols);
			}
			if (symbol_opt.has_value() && symbol_opt->is<VariableDeclarationNode>()) {
				resolved_object.var_decl = &symbol_opt->as<VariableDeclarationNode>();
				resolved_object.initializer = &resolved_object.var_decl->initializer();
				resolved_object.declared_type_index = resolved_object.var_decl->declaration().type_specifier_node().type_index();
			}
		}
		return std::nullopt;
	}

 // Try BoundOnly first (for statically-bound static members)
	if (auto static_member_result = resolve_current_struct_static_member(
			object_identifier,
			context,
			CurrentStructStaticLookupMode::BoundOnly);
		static_member_result.static_member) {
 // Phase C: if pre-packed bytes are available, materialize the struct and
 // provide the result directly so member access can read scalar fields from bytes.
 // Array/nested-struct members are excluded from the materialized bindings,
 // so callers will fall through to normal AST evaluation for those.
		if (static_member_result.static_member->normalized_init.has_value() &&
			static_member_result.static_member->normalized_init->isConstant()) {
			EvalResult materialized = materializeFromConstantBytes(
				static_member_result.static_member->normalized_init->constant_bytes,
				static_member_result.static_member->type_index,
				static_member_result.static_member->array_dimensions);
			if (materialized.success()) {
				resolved_object.declared_type_index = static_member_result.static_member->type_index;
				resolved_object.materialized_value = materialized;
 // Also set the raw initializer so that if a member lookup misses in the
 // materialized bindings (e.g., array members), the caller can fall through.
				resolved_object.initializer = &static_member_result.static_member->initializer;
				return std::nullopt;
			}
		}
		resolved_object.initializer = &static_member_result.static_member->initializer;
		resolved_object.declared_type_index = static_member_result.static_member->type_index;
		return std::nullopt;
	}

 // Also try PreferCurrentStruct if the identifier is unresolved/global
 // (template-copied expressions may not have StaticMember binding set)
	if (auto static_member_result = resolve_current_struct_static_member(
			object_identifier,
			context,
			CurrentStructStaticLookupMode::PreferCurrentStruct);
		static_member_result.static_member) {
		if (static_member_result.static_member->normalized_init.has_value() &&
			static_member_result.static_member->normalized_init->isConstant()) {
			EvalResult materialized = materializeFromConstantBytes(
				static_member_result.static_member->normalized_init->constant_bytes,
				static_member_result.static_member->type_index,
				static_member_result.static_member->array_dimensions);
			if (materialized.success()) {
				resolved_object.declared_type_index = static_member_result.static_member->type_index;
				resolved_object.materialized_value = materialized;
				resolved_object.initializer = &static_member_result.static_member->initializer;
				return std::nullopt;
			}
		}
		resolved_object.initializer = &static_member_result.static_member->initializer;
		resolved_object.declared_type_index = static_member_result.static_member->type_index;
		return std::nullopt;
	}

	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate " + std::string(usage_name) + ": no symbol table provided");
	}

	std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(object_identifier, object_name, *context.symbols);
	if (!symbol_opt.has_value() && context.global_symbols && context.global_symbols != context.symbols) {
		symbol_opt = lookup_identifier_symbol(object_identifier, object_name, *context.global_symbols);
	}
	if (!symbol_opt.has_value()) {
		return EvalResult::error("Undefined variable in " + std::string(usage_name) + ": " + std::string(object_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	if (!symbol_node.is<VariableDeclarationNode>()) {
		return EvalResult::error("Identifier in " + std::string(usage_name) + " is not a variable: " + std::string(object_name));
	}

	resolved_object.var_decl = &symbol_node.as<VariableDeclarationNode>();
	resolved_object.initializer = &resolved_object.var_decl->initializer();

	const DeclarationNode& decl = resolved_object.var_decl->declaration();
	{
		resolved_object.declared_type_index = decl.type_specifier_node().type_index();
	}

	return std::nullopt;
}

// Evaluate nested member access (e.g., obj.inner.value)
EvalResult Evaluator::evaluate_nested_member_access(
	const MemberAccessNode& inner_access,
	std::string_view final_member_name,
	EvaluationContext& context) {

	// First, we need to get the base object and the chain of member accesses
	// For obj.inner.value:
	// - inner_access.object() is 'obj' (identifier)
	// - inner_access.member_name() is 'inner'
	// - final_member_name is 'value'

	const ASTNode& base_obj_expr = inner_access.object();
	std::string_view intermediate_member = inner_access.member_name();

	// Get the base variable name
	std::string_view base_var_name;
	if (base_obj_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = base_obj_expr.as<ExpressionNode>();
		if (const auto* nested_access = std::get_if<MemberAccessNode>(&expr_node)) {
			EvalResult intermediate_result = evaluate_nested_member_access(*nested_access, intermediate_member, context);
			if (!intermediate_result.success()) {
				return intermediate_result;
			}

			auto final_member_it = intermediate_result.object_member_bindings.find(final_member_name);
			if (final_member_it != intermediate_result.object_member_bindings.end()) {
				return final_member_it->second;
			}
			if (intermediate_result.object_member_bindings.empty() &&
				!intermediate_result.object_type_index.is_valid()) {
				TypeIndex base_type_index;
				if (auto parsed_type_opt = Evaluator::tryQueryExpressionType(base_obj_expr, context); parsed_type_opt.has_value()) {
					base_type_index = parsed_type_opt->type_index();
				}
				if (base_type_index.is_valid()) {
					const TypeInfo* base_type_info = tryGetTypeInfo(base_type_index);
					const StructTypeInfo* base_struct_info = base_type_info ? base_type_info->getStructInfo() : nullptr;
					const StructMember* intermediate_member_info = base_struct_info ? base_struct_info->findMember(intermediate_member) : nullptr;
					if (intermediate_member_info &&
						is_struct_type(intermediate_member_info->type_index.category())) {
						const TypeInfo* intermediate_type_info = tryGetTypeInfo(intermediate_member_info->type_index);
						const StructTypeInfo* intermediate_struct_info = intermediate_type_info ? intermediate_type_info->getStructInfo() : nullptr;
						if (intermediate_struct_info) {
							auto ctor_resolution = resolve_constructor_overload_arity(*intermediate_struct_info, 1, true);
							const ConstructorDeclarationNode* matching_ctor = ctor_resolution.selected_overload;
							if (matching_ctor) {
								std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
								std::vector<EvalResult> ctor_args;
								ctor_args.push_back(std::move(intermediate_result));
								auto bind_result = bind_pre_evaluated_arguments(
									matching_ctor->parameter_nodes(),
									ctor_args,
									ctor_param_bindings,
									context,
									"Failed to bind constructor parameter for intermediate struct during deep nested member access",
									true);
								if (!bind_result.success()) {
									return bind_result;
								}

								EvalResult materialized_result = EvalResult::from_int(0LL);
								materialized_result.object_type_index = intermediate_member_info->type_index;
								auto materialize_result = materialize_members_from_constructor(
									intermediate_struct_info,
									*matching_ctor,
									ctor_param_bindings,
									materialized_result.object_member_bindings,
									context,
									false);
								if (!materialize_result.success()) {
									return materialize_result;
								}

								final_member_it = materialized_result.object_member_bindings.find(final_member_name);
								if (final_member_it != materialized_result.object_member_bindings.end()) {
									return final_member_it->second;
								}
							}
						}
					}
				}
			}

			return EvalResult::error(
				std::string(StringBuilder()
								.append("Final member '"sv)
								.append(final_member_name)
								.append("' not found in nested member access"sv)
								.commit()));
		}
	}

	const IdentifierNode* base_identifier = tryGetIdentifier(base_obj_expr);
	if (!base_identifier) {
		if (base_obj_expr.is<ExpressionNode>()) {
			EvalResult base_result = evaluate(base_obj_expr, context);
			if (!base_result.success()) {
				return base_result;
			}

			TypeIndex base_type_index = base_result.object_type_index;
			if (!base_type_index.is_valid()) {
				if (auto base_type_opt = try_get_type_from_eval_result(base_result); base_type_opt.has_value()) {
					base_type_index = base_type_opt->type_index();
				}
			}
			if (!base_type_index.is_valid()) {
				if (auto parsed_type_opt = Evaluator::tryQueryExpressionType(base_obj_expr, context); parsed_type_opt.has_value()) {
					base_type_index = parsed_type_opt->type_index();
				}
			}

			const TypeInfo* base_type_info = tryGetTypeInfo(base_type_index);
			if (!base_type_info) {
				return EvalResult::error("Base expression has invalid or out-of-bounds type index in nested member access");
			}

			const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
			if (!base_struct_info) {
				return EvalResult::error("Base expression in nested member access is not a struct object");
			}

			const StructMember* intermediate_member_info = base_struct_info->findMember(intermediate_member);
			if (!intermediate_member_info) {
				return EvalResult::error(
					std::string(StringBuilder()
									.append("Intermediate member '"sv)
									.append(intermediate_member)
									.append("' is not defined in the base struct type for nested member access"sv)
									.commit()));
			}

			auto intermediate_member_it = base_result.object_member_bindings.find(intermediate_member);
			if (intermediate_member_it == base_result.object_member_bindings.end()) {
				return EvalResult::error(
					std::string(StringBuilder()
									.append("Intermediate member '"sv)
									.append(intermediate_member)
									.append("' has no constexpr value in the evaluated base object for nested member access"sv)
									.commit()));
			}

			EvalResult intermediate_result = intermediate_member_it->second;
			const TypeInfo* intermediate_member_type_info = tryGetTypeInfo(intermediate_member_info->type_index);
			const bool needs_intermediate_materialization =
				!intermediate_result.object_type_index.is_valid() &&
				intermediate_result.object_member_bindings.empty() &&
				(is_struct_type(intermediate_member_info->type_index.category())) &&
				intermediate_member_type_info != nullptr;
			if (needs_intermediate_materialization) {
				if (const StructTypeInfo* intermediate_struct_info =
						intermediate_member_type_info->getStructInfo()) {
					auto ctor_resolution = resolve_constructor_overload_arity(*intermediate_struct_info, 1, true);
					const ConstructorDeclarationNode* matching_ctor = ctor_resolution.selected_overload;
					if (matching_ctor) {
						std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
						std::vector<EvalResult> ctor_args;
						ctor_args.push_back(intermediate_result);
						auto bind_result = bind_pre_evaluated_arguments(
							matching_ctor->parameter_nodes(),
							ctor_args,
							ctor_param_bindings,
							context,
							"Failed to bind constructor parameter while materializing intermediate struct member for nested member access",
							true);
						if (!bind_result.success()) {
							return bind_result;
						}

						EvalResult materialized_result = EvalResult::from_int(0LL);
						materialized_result.object_type_index = intermediate_member_info->type_index;
						auto materialize_result = materialize_members_from_constructor(
							intermediate_struct_info,
							*matching_ctor,
							ctor_param_bindings,
							materialized_result.object_member_bindings,
							context,
							false);
						if (!materialize_result.success()) {
							return materialize_result;
						}
						intermediate_result = std::move(materialized_result);
					}
				}
			}

			auto final_member_it = intermediate_result.object_member_bindings.find(final_member_name);
			if (final_member_it != intermediate_result.object_member_bindings.end()) {
				return final_member_it->second;
			}

			return EvalResult::error(
				std::string(StringBuilder()
								.append("Final member '"sv)
								.append(final_member_name)
								.append("' not found in nested member access"sv)
								.commit()));
		}
		return EvalResult::error("Invalid base expression in nested member access");
	}
	base_var_name = base_identifier->name();

	// Prefer active local constexpr bindings for block-scoped objects (e.g. local
	// variables inside constexpr functions).  Nested member access like
	// `Outer<int> o(...); return o.inner.value;` should resolve from the current
	// evaluation bindings before falling back to symbol-table/global lookups.
	if (const EvalResult* local_bound = findLocalBinding(base_var_name, context)) {
		const EvalResult* base_value = resolveReadThroughReferenceAlias(
			*local_bound,
			emptyEvalBindings(),
			context);
		if (!base_value) {
			return EvalResult::error("Dangling reference binding in nested member access: " + std::string(base_var_name));
		}

		auto intermediate_member_it = base_value->object_member_bindings.find(intermediate_member);
		if (intermediate_member_it == base_value->object_member_bindings.end()) {
			return EvalResult::error(
				std::string(StringBuilder()
								.append("Intermediate member '"sv)
								.append(intermediate_member)
								.append("' has no constexpr value in the evaluated base object for nested member access"sv)
								.commit()));
		}

		const EvalResult& intermediate_value = intermediate_member_it->second;
		auto final_member_it = intermediate_value.object_member_bindings.find(final_member_name);
		if (final_member_it != intermediate_value.object_member_bindings.end()) {
			return final_member_it->second;
		}
	}

	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
			base_identifier,
			base_var_name,
			context,
			"nested member access",
			resolved_object)) {
		return *resolve_error;
	}

	// If the base object was already materialized from local/static constexpr
	// bindings, resolve nested members directly from that value before requiring
	// an AST initializer.
	if (resolved_object.materialized_value.has_value()) {
		const EvalResult& base_value = *resolved_object.materialized_value;
		auto intermediate_member_it = base_value.object_member_bindings.find(intermediate_member);
		if (intermediate_member_it != base_value.object_member_bindings.end()) {
			const EvalResult& intermediate_value = intermediate_member_it->second;
			auto final_member_it = intermediate_value.object_member_bindings.find(final_member_name);
			if (final_member_it != intermediate_value.object_member_bindings.end()) {
				return final_member_it->second;
			}
		}
	}

	if (resolved_object.initializer == nullptr) {
		return EvalResult::error("Internal error: unresolved nested member access object source");
	}

	if (resolved_object.var_decl && !resolved_object.var_decl->is_constexpr()) {
		return EvalResult::error("Variable in nested member access must be constexpr");
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex base_declared_type_index = resolved_object.declared_type_index;

	if (!initializer->has_value()) {
		return EvalResult::error("Constexpr variable has no initializer in nested member access");
	}

	ResolvedConstexprMemberSource intermediate_member_source;
	if (auto resolve_error = resolve_constexpr_member_source_from_initializer(
			*initializer,
			base_declared_type_index,
			intermediate_member,
			"nested member access",
			context,
			intermediate_member_source)) {
		return *resolve_error;
	}

	if ((!intermediate_member_source.initializer.has_value() && !intermediate_member_source.value.has_value()) || !intermediate_member_source.member_info) {
		return EvalResult::error("Internal error: unresolved intermediate member source in nested member access");
	}

	const StructMember* intermediate_member_info = intermediate_member_source.member_info;
	if (!is_struct_type(intermediate_member_info->type_index.category())) {
		return EvalResult::error("Intermediate member is not a struct type");
	}

	TypeIndex inner_type_index = intermediate_member_info->type_index;
	const auto* intermediate_bindings = intermediate_member_source.evaluation_bindings.empty()
											? nullptr
											: &intermediate_member_source.evaluation_bindings;

	if (intermediate_member_source.value.has_value()) {
		EvalResult intermediate_value = intermediate_member_source.value.value();
		auto final_member_it = intermediate_value.object_member_bindings.find(final_member_name);
		if (final_member_it != intermediate_value.object_member_bindings.end()) {
			return final_member_it->second;
		}
		if (intermediate_member_source.member_info &&
			!intermediate_value.object_type_index.is_valid() &&
			intermediate_value.object_member_bindings.empty() &&
			is_struct_type(intermediate_member_source.member_info->type_index.category())) {
			const TypeIndex intermediate_type_index = intermediate_member_source.member_info->type_index;
			const TypeInfo* intermediate_type_info = tryGetTypeInfo(intermediate_type_index);
			const StructTypeInfo* intermediate_struct_info = intermediate_type_info ? intermediate_type_info->getStructInfo() : nullptr;
			if (intermediate_struct_info) {
				auto ctor_resolution = resolve_constructor_overload_arity(*intermediate_struct_info, 1, true);
				const ConstructorDeclarationNode* matching_ctor = ctor_resolution.selected_overload;
				if (matching_ctor) {
					std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
					std::vector<EvalResult> ctor_args;
					ctor_args.push_back(intermediate_value);
					auto bind_result = bind_pre_evaluated_arguments(
						matching_ctor->parameter_nodes(),
						ctor_args,
						ctor_param_bindings,
						context,
						"Failed to bind constructor parameter while materializing nested constexpr member access",
						true);
					if (!bind_result.success()) {
						return bind_result;
					}

					EvalResult materialized_value = EvalResult::from_int(0LL);
					materialized_value.object_type_index = intermediate_type_index;
					auto materialize_result = materialize_members_from_constructor(
						intermediate_struct_info,
						*matching_ctor,
						ctor_param_bindings,
						materialized_value.object_member_bindings,
						context,
						false);
					if (!materialize_result.success()) {
						return materialize_result;
					}

					final_member_it = materialized_value.object_member_bindings.find(final_member_name);
					if (final_member_it != materialized_value.object_member_bindings.end()) {
						return final_member_it->second;
					}
				}
			}
		}
		if (!intermediate_member_source.initializer.has_value()) {
			return EvalResult::error("Final member '" + std::string(final_member_name) + "' not found in inner struct");
		}
	}

	if (intermediate_member_source.initializer->is<InitializerListNode>() ||
		extract_constructor_call(intermediate_member_source.initializer)) {
		ResolvedConstexprMemberSource final_member_source;
		if (auto final_error = resolve_constexpr_member_source_from_initializer(
				intermediate_member_source.initializer,
				inner_type_index,
				final_member_name,
				"nested member access",
				context,
				final_member_source,
				intermediate_bindings)) {
			return *final_error;
		}

		if (final_member_source.value.has_value()) {
			return final_member_source.value.value();
		}

		if (!final_member_source.initializer.has_value()) {
			return EvalResult::error("Internal error: unresolved final member source in nested member access");
		}

		if (!final_member_source.evaluation_bindings.empty()) {
			return evaluate_expression_with_bindings(
				final_member_source.initializer.value(),
				final_member_source.evaluation_bindings,
				context);
		}

		return evaluate(final_member_source.initializer.value(), context);
	}

	const TypeInfo* inner_type_info = tryGetTypeInfo(inner_type_index);
	if (!inner_type_info) {
		return EvalResult::error("Invalid inner type index");
	}

	const StructTypeInfo* inner_struct_info = inner_type_info->getStructInfo();
	if (!inner_struct_info) {
		return EvalResult::error("Inner member type is not a struct");
	}

	const ASTNode& intermediate_init = intermediate_member_source.initializer.value();
	if ((*initializer)->is<InitializerListNode>()) {
		StringHandle final_handle = StringTable::getOrInternStringHandle(final_member_name);
		if (!inner_struct_info->members.empty() && inner_struct_info->members[0].getName() == final_handle) {
			if (intermediate_bindings) {
				return evaluate_expression_with_bindings_const(intermediate_init, *intermediate_bindings, context);
			}
			return evaluate(intermediate_init, context);
		}

		return EvalResult::error("Final member '" + std::string(final_member_name) +
								 "' not reachable via scalar initializer (brace elision) in nested aggregate");
	}

	EvalResult init_arg_result = intermediate_bindings
									 ? evaluate_expression_with_bindings_const(intermediate_init, *intermediate_bindings, context)
									 : evaluate(intermediate_init, context);
	if (!init_arg_result.success()) {
		return init_arg_result;
	}

	auto inner_ctor_resolution = resolve_constructor_overload_arity(*inner_struct_info, 1, true);
	const ConstructorDeclarationNode* inner_matching_ctor = inner_ctor_resolution.selected_overload;

	if (!inner_matching_ctor) {
		return EvalResult::error("No matching single-argument constructor for inner struct");
	}

	std::unordered_map<std::string_view, EvalResult> inner_param_bindings;
	const auto& inner_params = inner_matching_ctor->parameter_nodes();
	std::vector<EvalResult> inner_ctor_args;
	inner_ctor_args.push_back(init_arg_result);
	auto bind_result = bind_pre_evaluated_arguments(
		inner_params,
		inner_ctor_args,
		inner_param_bindings,
		context,
		"Invalid parameter node in inner constexpr constructor binding",
		true);
	if (!bind_result.success()) {
		return bind_result;
	}

	if (auto member_result = try_evaluate_member_from_constructor_initializers(
			inner_struct_info,
			*inner_matching_ctor,
			inner_param_bindings,
			final_member_name,
			context)) {
		return *member_result;
	}

	return EvalResult::error("Final member '" + std::string(final_member_name) + "' not found in inner struct");
}

// Evaluate array subscript followed by member access (e.g., arr[0].member)
EvalResult Evaluator::evaluate_array_subscript_member_access(
	const ArraySubscriptNode& subscript,
	std::string_view member_name,
	EvaluationContext& context) {
	auto get_array_elements_for_identifier =
		[&context](const IdentifierNode& identifier, bool& found_preferred_static_member) -> std::optional<std::vector<ASTNode>> {
		std::string_view array_name = identifier.name();
		found_preferred_static_member = false;

		if (!context.symbols) {
			return std::nullopt;
		}

		auto extract_elements = [](const std::optional<ASTNode>& initializer) -> std::optional<std::vector<ASTNode>> {
			if (!initializer.has_value() || !initializer->is<InitializerListNode>()) {
				return std::nullopt;
			}

			const InitializerListNode& init_list = initializer->as<InitializerListNode>();
			const std::span<const ASTNode> initializers = init_list.initializers();
			return std::vector<ASTNode>(initializers.begin(), initializers.end());
		};

		if (auto static_member_result = resolve_current_struct_static_member(
				&identifier,
				context,
				CurrentStructStaticLookupMode::PreferCurrentStruct);
			static_member_result.static_member) {
			found_preferred_static_member = true;
			if (auto elements = extract_elements(static_member_result.static_member->initializer)) {
				return elements;
			}

			StringHandle qualified_handle = StringTable::getOrInternStringHandle(
				StringBuilder().append(static_member_result.owner_struct->getName()).append("::"sv).append(identifier.nameHandle()).commit());
			auto qualified_symbol = context.symbols->lookup(qualified_handle);
			if (qualified_symbol.has_value() && qualified_symbol->is<VariableDeclarationNode>()) {
				const VariableDeclarationNode& qualified_var = qualified_symbol->as<VariableDeclarationNode>();
				if (qualified_var.is_constexpr()) {
					if (auto elements = extract_elements(qualified_var.initializer())) {
						return elements;
					}
				}
			}

			return std::nullopt;
		}

		std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(&identifier, array_name, *context.symbols);
		if (!symbol_opt.has_value()) {
			return std::nullopt;
		}

		const ASTNode& symbol_node = symbol_opt.value();
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return std::nullopt;
		}

		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		if (!var_decl.is_constexpr()) {
			return std::nullopt;
		}

		return extract_elements(var_decl.initializer());
	};

	auto extractConstructorCall = [](const ASTNode& element) -> const ConstructorCallNode* {
		if (element.is<ConstructorCallNode>()) {
			return &element.as<ConstructorCallNode>();
		}
		if (!element.is<ExpressionNode>()) {
			return nullptr;
		}

		const ExpressionNode& element_expr = element.as<ExpressionNode>();
		if (const auto* constructor_call = std::get_if<ConstructorCallNode>(&element_expr)) {
			return constructor_call;
		}
		return nullptr;
	};

	auto evaluateMemberFromCtorCall = [&context, member_name](const ConstructorCallNode& ctor_call) -> EvalResult {
		const TypeSpecifierNode& type_spec = ctor_call.type_node();
		const StructTypeInfo* struct_info = get_struct_info_from_type(type_spec);
		if (!struct_info) {
			return EvalResult::error("Array element is not a struct in member access");
		}

		const auto& ctor_args = ctor_call.arguments();
		const ConstructorDeclarationNode* matching_ctor = ctor_call.resolved_constructor();
		if (!matching_ctor) {
			auto ctor_candidates = struct_info->getConstructorsByParameterCount(ctor_args.size(), false);
			for (const StructMemberFunction* candidate : ctor_candidates) {
				if (!candidate || !candidate->function_decl.is<ConstructorDeclarationNode>()) {
					continue;
				}
				const ConstructorDeclarationNode& candidate_ctor = candidate->function_decl.as<ConstructorDeclarationNode>();
				const auto& params = candidate_ctor.parameter_nodes();
				bool has_static_cast_arg = false;
				bool matches_cast_targets = true;
				for (size_t i = 0; i < params.size() && i < ctor_args.size(); ++i) {
					if (!ctor_args[i].is<ExpressionNode>()) {
						continue;
					}
					const ExpressionNode& arg_expr = ctor_args[i].as<ExpressionNode>();
					if (!std::holds_alternative<StaticCastNode>(arg_expr)) {
						continue;
					}
					has_static_cast_arg = true;
					if (!params[i].is<DeclarationNode>()) {
						matches_cast_targets = false;
						break;
					}

					const StaticCastNode& cast_node = std::get<StaticCastNode>(arg_expr);
					const TypeSpecifierNode& cast_type = cast_node.target_type();
					const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
					const TypeSpecifierNode& param_type = param_decl.type_specifier_node();
					if (cast_type.type() != param_type.type() ||
						cast_type.type_index() != param_type.type_index() ||
						cast_type.pointer_depth() != param_type.pointer_depth()) {
						matches_cast_targets = false;
						break;
					}
				}

				if (has_static_cast_arg && matches_cast_targets) {
					matching_ctor = &candidate_ctor;
					break;
				}
			}
		}
		if (!matching_ctor) {
			matching_ctor = find_matching_constructor(struct_info, ctor_args, context, false, nullptr);
		}
		if (!matching_ctor) {
			return EvalResult::error("No matching constructor found for constexpr array element");
		}

		std::vector<EvalResult> evaluated_ctor_args;
		evaluated_ctor_args.reserve(ctor_args.size());
		for (const auto& ctor_arg : ctor_args) {
			auto arg_result = evaluate(ctor_arg, context);
			if (!arg_result.success()) {
				return arg_result;
			}
			evaluated_ctor_args.push_back(arg_result);
		}

		std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
		const auto& params = matching_ctor->parameter_nodes();
		auto bind_result = bind_pre_evaluated_arguments(
			params,
			evaluated_ctor_args,
			ctor_param_bindings,
			context,
			"Invalid parameter node in array element constructor binding",
			true);
		if (!bind_result.success()) {
			return bind_result;
		}

		if (auto member_result = try_evaluate_member_from_constructor_initializers(
				struct_info,
				*matching_ctor,
				ctor_param_bindings,
				member_name,
				context)) {
			return *member_result;
		}

		return EvalResult::error("Member '" + std::string(member_name) + "' not found in array element");
	};

	auto index_result = evaluate(subscript.index_expr(), context);
	if (!index_result.success()) {
		return index_result;
	}

	long long evaluated_index = index_result.as_int();
	if (evaluated_index < 0) {
		return EvalResult::error("Negative array index in constant expression");
	}
	if (static_cast<unsigned long long>(evaluated_index) > std::numeric_limits<size_t>::max()) {
		return EvalResult::error("Array index too large in constant expression");
	}

	const ASTNode& array_expr = subscript.array_expr();
	const IdentifierNode* array_identifier = nullptr;
	if (array_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr = array_expr.as<ExpressionNode>();
		if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&expr)) {
			array_identifier = identifier_ptr;
		}
	} else if (array_expr.is<IdentifierNode>()) {
		array_identifier = &array_expr.as<IdentifierNode>();
	}
	if (!array_identifier) {
		return EvalResult::error("Array subscript followed by member access requires identifier base");
	}

	bool found_preferred_static_member = false;
	auto elements = get_array_elements_for_identifier(*array_identifier, found_preferred_static_member);
	if (!elements) {
		if (found_preferred_static_member) {
			return EvalResult::error("Array subscript member access requires a constexpr initializer list on the preferred static member array");
		}
		return EvalResult::error("Array subscript member access requires constexpr array initializer list");
	}

	size_t element_index = static_cast<size_t>(evaluated_index);
	if (element_index >= elements->size()) {
		return EvalResult::error("Array index " + std::to_string(element_index) + " out of bounds (size " + std::to_string(elements->size()) + ")");
	}

	const ASTNode& array_element = (*elements)[element_index];
	const ConstructorCallNode* ctor_call_ptr = extractConstructorCall(array_element);
	if (!ctor_call_ptr) {
		return EvalResult::error("Array element in member access is not a constructor call");
	}

	return evaluateMemberFromCtorCall(*ctor_call_ptr);
}

EvalResult Evaluator::evaluate_static_member_initializer_or_default(
	const StructStaticMember& static_member,
	EvaluationContext& context) {
	if (gEvaluatingStaticMembers.contains(&static_member)) {
		return EvalResult::error("Circular dependency between static member initializers");
	}
	StaticMemberEvaluationGuard guard(&static_member);

	if (static_member.initializer.has_value()) {
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded");
		}
		context.current_depth++;
		EvalResult result = static_member.is_array && static_member.initializer->is<InitializerListNode>()
								? materializeArrayInitializer(
									static_member.type_index,
									static_member.array_dimensions,
									static_member.initializer->as<InitializerListNode>(),
									context)
								: evaluate(static_member.initializer.value(), context);
		context.current_depth--;
		if (!result.success() && IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
			FLASH_LOG(ConstExpr, Debug, "Static member initializer evaluation failed: ", result.error_message);
		}
		return result;
	}

	if (static_member.is_constexpr) {
		return EvalResult::error(
			"constexpr static data member has no initializer",
			EvalErrorType::NotConstantExpression);
	}

	if (static_member.type_index.category() == TypeCategory::Bool) {
		return EvalResult::from_bool(false);
	}
	return EvalResult::from_int(0);
}

EvalResult Evaluator::tryReadStaticMemberConstant(
	const StructStaticMember& static_member,
	EvaluationContext& context,
	bool scalar_only) {
	if (!static_member.is_constexpr) {
		return EvalResult::error(
			"static data member is not constexpr",
			EvalErrorType::NotConstantExpression);
	}

	auto attach_exact_static_type = [&static_member](EvalResult result) -> EvalResult {
		if (!result.success()) {
			return result;
		}
		TypeIndex type_index = static_member.type_index.is_valid()
			? static_member.type_index
			: nativeTypeIndex(static_member.memberType());
		const int size_bits = static_member.size != 0
			? static_cast<int>(static_member.size * 8)
			: get_type_size_bits(static_member.memberType());
		TypeSpecifierNode exact_type(
			type_index.withCategory(static_member.memberType()),
			TypeQualifier::None,
			size_bits,
			Token{},
			static_member.cv_qualifier);
		if (static_member.pointer_depth > 0) {
			exact_type.add_pointer_levels(static_member.pointer_depth);
		}
		if (static_member.reference_qualifier != ReferenceQualifier::None) {
			exact_type.set_reference_qualifier(static_member.reference_qualifier);
		}
		if (static_member.is_array) {
			exact_type.set_array_dimensions(static_member.array_dimensions);
		}
		result.set_exact_type(exact_type);
		return result;
	};

	if (static_member.normalized_init.has_value() &&
		static_member.normalized_init->isConstant()) {
		EvalResult materialized = materializeFromConstantBytes(
			static_member.normalized_init->constant_bytes,
			static_member.type_index,
			static_member.array_dimensions);
		if (materialized.success()) {
			if (scalar_only &&
				(materialized.is_array || materialized.object_type_index.is_valid())) {
				return EvalResult::error(
					"constexpr static data member is not a scalar constant",
					EvalErrorType::NotConstantExpression);
			}
			return attach_exact_static_type(std::move(materialized));
		}
	}

	const size_t saved_max_depth = context.max_recursion_depth;
	const ConstExpr::StorageDuration saved_storage_duration = context.storage_duration;
	context.max_recursion_depth = std::min<size_t>(context.max_recursion_depth, 64);
	context.storage_duration = ConstExpr::StorageDuration::Automatic;
	EvalResult result = evaluate_static_member_initializer_or_default(static_member, context);
	context.storage_duration = saved_storage_duration;
	context.max_recursion_depth = saved_max_depth;

	if (result.success() && scalar_only &&
		(result.is_array || result.object_type_index.is_valid())) {
		return EvalResult::error(
			"constexpr static data member is not a scalar constant",
			EvalErrorType::NotConstantExpression);
	}
	return attach_exact_static_type(std::move(result));
}

// Helper function to look up and evaluate static member from struct info
EvalResult Evaluator::evaluate_static_member_from_struct(
	const StructTypeInfo* struct_info,
	const TypeInfo& type_info,
	StringHandle member_name_handle,
	std::string_view member_name,
	EvaluationContext& context) {
	if (context.current_depth >= context.max_recursion_depth) {
		return EvalResult::error("Constexpr recursion depth limit exceeded");
	}

	auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);

	if (!static_member) {
		return EvalResult::error("Member '" + std::string(member_name) + "' not found in return type");
	}

	if (static_member->initializer.has_value()) {
		context.current_depth++;
		EvalResult result = static_member->is_array && static_member->initializer->is<InitializerListNode>()
								? materializeArrayInitializer(
									static_member->type_index,
									static_member->array_dimensions,
									static_member->initializer->as<InitializerListNode>(),
									context)
								: evaluate(*static_member->initializer, context);
		context.current_depth--;
		return result;
	}

	StringBuilder qualified_name_builder;
	qualified_name_builder.append(StringTable::getStringView(owner_struct ? owner_struct->getName() : type_info.name_));
	qualified_name_builder.append("::");
	qualified_name_builder.append(member_name);
	std::string_view qualified_member_name = qualified_name_builder.commit();

	auto member_symbol = context.symbols->lookup(qualified_member_name);
	if (member_symbol.has_value()) {
		const ASTNode& member_node = member_symbol.value();
		if (member_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = member_node.as<VariableDeclarationNode>();
			if (var_decl.is_constexpr() && var_decl.initializer().has_value()) {
				context.current_depth++;
				EvalResult result =
					var_decl.initializer()->is<InitializerListNode>() &&
							var_decl.declaration().type_specifier_node().array_dimension_count() > 0
						? materializeArrayInitializer(
							var_decl.declaration().type_specifier_node().type_index(),
							var_decl.declaration().type_specifier_node().array_dimensions(),
							var_decl.initializer()->as<InitializerListNode>(),
							context)
						: evaluate(*var_decl.initializer(), context);
				context.current_depth--;
				return result;
			}
		}
	}

	return EvalResult::error("Static member '" + std::string(member_name) + "' found but has no constexpr initializer");
}

EvalResult Evaluator::evaluate_function_call_member_access(
	const CallExprNode& call_expr,
	std::string_view member_name,
	EvaluationContext& context) {
	const CallInfo call_info = CallInfo::from(call_expr);
	const DeclarationNode& func_decl_node = *call_info.declaration;
	StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);

	const ASTNode& type_node = func_decl_node.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Function return type is not a TypeSpecifierNode");
	}

	const TypeSpecifierNode& return_type = type_node.as<TypeSpecifierNode>();
	if (!is_struct_type(return_type.category())) {
		return EvalResult::error("Function return type is not a struct - cannot access member");
	}

	TypeIndex type_index = return_type.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	if (!type_info) {
		return EvalResult::error("Invalid type index for function return type");
	}

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info) {
		return EvalResult::error("Return type is not a struct");
	}

	return evaluate_static_member_from_struct(struct_info, *type_info, member_name_handle, member_name, context);
}


EvalResult Evaluator::evaluate_member_function_call(const CallExprNode& call_expr, EvaluationContext& context) {
	// Check recursion depth
	if (context.current_depth >= context.max_recursion_depth) {
		return EvalResult::error("Constexpr recursion depth limit exceeded in member function call");
	}

	// Get the object being called on
	if (!call_expr.has_receiver()) {
		return EvalResult::error("Constexpr member function call is missing a receiver");
	}
	const ASTNode& object_expr = call_expr.receiver();

	// Get the function name from the placeholder FunctionDeclarationNode
	const FunctionDeclarationNode* placeholder_func = call_expr.callee().function_declaration_or_null();
	if (!placeholder_func) {
		return EvalResult::error("Constexpr member function call is missing a function declaration");
	}
	std::string_view func_name = normalizeConstexprLookupName(
		placeholder_func->decl_node().identifier_token().value());

	// For lambda calls (operator()), we need special handling
	const bool is_operator_call = overloadableOperatorFromFunctionName(func_name) == OverloadableOperator::Call;

	if (is_operator_call) {
		auto extract_lambda_from_object_expr = [&]() -> const LambdaExpressionNode* {
			if (object_expr.is<LambdaExpressionNode>()) {
				return &object_expr.as<LambdaExpressionNode>();
			}
			if (object_expr.is<ExpressionNode>()) {
				const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
				if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&expr_node)) {
					return lambda_expression;
				}
			}
			return nullptr;
		};

		if (const LambdaExpressionNode* object_lambda = extract_lambda_from_object_expr()) {
			return evaluate_lambda_call(*object_lambda, call_expr.arguments(), context);
		}
	}

	auto try_evaluate_current_struct_static_member = [&]() -> std::optional<EvalResult> {
		const FunctionDeclarationNode* matched_function = try_get_lowered_constexpr_member_call_target(
			call_expr,
			context.struct_info,
			call_expr.arguments().size(),
			context,
			MemberFunctionLookupMode::ConstexprEvaluable,
			true);
		if (!matched_function) {
			StringHandle fn_handle = StringTable::getOrInternStringHandle(func_name);
			auto current_match = resolveConstAwareConstexprMemberFunctionCallCandidate(
				context.struct_info,
				fn_handle,
				call_expr.arguments(),
				context,
				MemberFunctionLookupMode::ConstexprEvaluable,
				true,
				context.current_member_function_is_const,
				true);
			matched_function = current_match.function;
		}

		if (!matched_function) {
			return std::nullopt;
		}

		std::unordered_map<std::string_view, EvalResult> empty_b;
		return evaluate_function_call_with_template_context(
			*matched_function,
			call_expr.arguments(),
			empty_b,
			context,
			nullptr,
			FunctionCallTemplateBindingLoadMode::ForceCurrentStructIfAvailable);
	};

	// First, we need to get the struct type from the object to look up the actual function
	std::string_view var_name;
	const IdentifierNode* object_identifier = nullptr;
	EvalResult complex_object_result;
	bool has_complex_object_result = false;

	auto extracted = extract_identifier_from_expression(object_expr);
	if (!extracted) {
		bool resolved_from_local_bindings = false;
		if (context.local_bindings) {
			ResolvedBoundEvalResult resolved_object = resolve_bound_eval_result(
				object_expr,
				*context.local_bindings,
				context,
				true);
			if (resolved_object.error.has_value()) {
				return resolved_object.error.value();
			}
			if (resolved_object.value) {
				complex_object_result = *resolved_object.value;
				resolved_from_local_bindings = true;
			}
		}
		if (!resolved_from_local_bindings) {
			complex_object_result = evaluate(object_expr, context);
			if (!complex_object_result.success()) {
				return complex_object_result;
			}
		}
		has_complex_object_result = true;
	} else {
		object_identifier = extracted->identifier;
		var_name = extracted->name;
	}

	if (placeholder_func->is_static() && object_identifier && object_identifier->name() == "this") {
		if (auto static_member_result = try_evaluate_current_struct_static_member()) {
			return *static_member_result;
		}
	}

	const VariableDeclarationNode* var_decl = nullptr;
	ResolvedConstexprObject resolved_object;
	const std::optional<ASTNode>* initializer = nullptr;
	TypeIndex declared_type_index{0};

	if (!has_complex_object_result) {
		if (auto resolve_error = resolve_constexpr_object_source(
				object_identifier,
				var_name,
				context,
				"member function call",
				resolved_object)) {
			return *resolve_error;
		}

		var_decl = resolved_object.var_decl;
		declared_type_index = resolved_object.declared_type_index;
		if (resolved_object.materialized_value.has_value()) {
			complex_object_result = *resolved_object.materialized_value;
			has_complex_object_result = true;
		} else {
			if (var_decl && !var_decl->is_constexpr()) {
				return EvalResult::error("Variable in member function call must be constexpr: " + std::string(var_name));
			}

			initializer = resolved_object.initializer;
			if (!initializer || !initializer->has_value()) {
				return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
			}
		}

		if (!var_decl) {
			if (auto static_member_result = try_evaluate_current_struct_static_member()) {
				return *static_member_result;
			}
		}
	}

	if (has_complex_object_result && complex_object_result.pointer_to_var.isValid()) {
		std::unordered_map<std::string_view, EvalResult> no_local_bindings;
		complex_object_result = deref_pointer_with_bindings(complex_object_result, no_local_bindings, context);
		if (!complex_object_result.success()) {
			return complex_object_result;
		}
	}

	if (!has_complex_object_result && initializer && initializer->has_value()) {
		bool receiver_is_pointer = false;
		if (var_decl) {
			const TypeSpecifierNode& receiver_type = var_decl->declaration().type_specifier_node();
			receiver_is_pointer = receiver_type.is_pointer();
		}
		if (receiver_is_pointer) {
			EvalResult pointer_result = evaluate(initializer->value(), context);
			if (!pointer_result.success()) {
				return pointer_result;
			}
			if (!pointer_result.pointer_to_var.isValid()) {
				return EvalResult::error("Member function call receiver is not a constexpr object pointer");
			}
			std::unordered_map<std::string_view, EvalResult> no_local_bindings;
			complex_object_result = deref_pointer_with_bindings(pointer_result, no_local_bindings, context);
			if (!complex_object_result.success()) {
				return complex_object_result;
			}
			has_complex_object_result = true;
		}
	}

	// Check if this is a lambda call (operator() on a lambda object)
	if (is_operator_call && !has_complex_object_result) {
		if (initializer && initializer->has_value()) {
			if (auto callable_from_initializer = try_evaluate_callable_initializer_for_call(
					initializer->value(),
					call_expr.arguments(),
					context,
					call_expr.callee().function_declaration_or_null())) {
				return *callable_from_initializer;
			}
		}
		const LambdaExpressionNode* lambda = extract_lambda_from_initializer(*initializer);
		if (lambda) {
			return evaluate_lambda_call(*lambda, call_expr.arguments(), context);
		}
		// Brace-initialized or ConstructorCallNode-initialized callable: delegate to evaluate_callable_object
		if (initializer->has_value() && ((*initializer)->is<InitializerListNode>() || extract_constructor_call(*initializer))) {
			if (!var_decl) {
				return EvalResult::error("Callable object is not a variable");
			}
			return evaluate_callable_object(
				*var_decl,
				call_expr.arguments(),
				context,
				nullptr,
				nullptr,
				nullptr,
				call_expr.callee().function_declaration_or_null());
		}
	}

	const ConstructorCallNode* ctor_call_ptr =
		has_complex_object_result ? nullptr : extract_constructor_call(*initializer);
	if (!has_complex_object_result && !ctor_call_ptr && !(*initializer)->is<InitializerListNode>()) {
		return EvalResult::error("Member function calls require struct/class objects");
	}

	// Resolve the struct type info. For ConstructorCallNode initializers we get it from
	// the constructor's type node; for brace-initialized (InitializerListNode) objects we
	// resolve it from the variable's declared type instead.
	const StructTypeInfo* struct_info = nullptr;
	TypeIndex type_index{0};

	if (has_complex_object_result) {
		type_index = complex_object_result.object_type_index;
		declared_type_index = type_index;
		if (const TypeInfo* object_type_info = tryGetTypeInfo(type_index)) {
			struct_info = object_type_info->getStructInfo();
		}
	} else if (ctor_call_ptr) {
		const ConstructorCallNode& ctor_call = *ctor_call_ptr;
		const TypeSpecifierNode& type_spec = ctor_call.type_node();
		if (!is_struct_type(type_spec.category())) {
			return EvalResult::error("Member function call requires a struct type");
		}
		type_index = type_spec.type_index();
		if (const TypeInfo* ctor_type_info = tryGetTypeInfo(type_index)) {
			struct_info = ctor_type_info->getStructInfo();
		}
		if (!struct_info) {
			if (const TypeInfo* declared_type_info = tryGetTypeInfo(declared_type_index)) {
				type_index = TypeIndex{declared_type_index};
				struct_info = declared_type_info->getStructInfo();
			}
		}
	} else {
		// Brace-initialized object: resolve type from the declared object type.
		const TypeInfo* declared_type_info = tryGetTypeInfo(declared_type_index);
		if (!declared_type_info) {
			return EvalResult::error("Brace-initialized object has invalid type in member function call");
		}
		type_index = TypeIndex{declared_type_index};
		struct_info = declared_type_info->getStructInfo();
	}

	if (!struct_info) {
		return EvalResult::error("Type is not a struct in member function call");
	}

	bool receiver_is_const = false;
	if (has_complex_object_result && complex_object_result.exact_type.has_value()) {
		receiver_is_const = complex_object_result.exact_type->is_const();
	}
	if (!receiver_is_const && var_decl) {
		receiver_is_const = var_decl->declaration().type_specifier_node().is_const() ||
			var_decl->is_constexpr();
	}
	if (!receiver_is_const) {
		if (std::optional<TypeSpecifierNode> receiver_expr_type =
				tryQueryExpressionType(object_expr, context);
			receiver_expr_type.has_value()) {
			receiver_is_const = receiver_expr_type->is_const();
		}
	}
	if (!receiver_is_const && object_identifier && context.symbols) {
		std::optional<ASTNode> object_symbol = lookup_identifier_symbol(
			object_identifier,
			object_identifier->name(),
			*context.symbols);
		if (!object_symbol.has_value() && context.global_symbols) {
			object_symbol = lookup_identifier_symbol(
				object_identifier,
				object_identifier->name(),
				*context.global_symbols);
		}
		if (object_symbol.has_value() && object_symbol->is<VariableDeclarationNode>()) {
			const auto& object_var_decl = object_symbol->as<VariableDeclarationNode>();
			receiver_is_const = object_var_decl.is_constexpr() ||
				object_var_decl.declaration().type_specifier_node().is_const();
		}
	}

	// Look up the actual member function in the struct's type info
	const auto& arguments = call_expr.arguments();
	StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
	auto lookup_sema_services =
		context
			.requireParserAttachedSema(kMemberFunctionMaterializationLookupOp)
			.parserSemanticServices();
	ResolvedFunctionQueryResult sema_resolved_direct_query =
		lookup_sema_services.getResolvedDirectCallQuery(&call_expr);
	if (placeholder_func) {
		lookup_sema_services.ensureMemberFunctionMaterialized(
			struct_info->name,
			*placeholder_func);
	}
	lookup_sema_services.ensureMemberFunctionMaterialized(
		struct_info->name,
		func_name_handle,
		std::nullopt);
	ResolvedMemberFunctionCandidate member_function_match =
		resolveConstAwareConstexprMemberFunctionCallCandidate(
			struct_info,
			func_name_handle,
			arguments,
			context,
			MemberFunctionLookupMode::LookupOnly,
			false,
			receiver_is_const,
			true);
	if (member_function_match.ambiguous) {
		return EvalResult::error("Ambiguous member function overload in constant expression");
	}
	if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
		FLASH_LOG(ConstExpr, Debug,
			"evaluate_member_function_call: func='", func_name,
			"' struct='", StringTable::getStringView(struct_info->name),
			"' type_index=", type_index,
			" sema_state=", static_cast<int>(sema_resolved_direct_query.state),
			" placeholder=", placeholder_func != nullptr,
			" matched=", member_function_match.function != nullptr);
		if (placeholder_func) {
			FLASH_LOG(ConstExpr, Debug,
				"  placeholder name='", placeholder_func->decl_node().identifier_token().value(),
				"' constexpr=", placeholder_func->is_constexpr(),
				" materialized=", placeholder_func->is_materialized(),
				" outer_template_bindings=", placeholder_func->has_outer_template_bindings(),
				" non_type_template_args=", placeholder_func->has_non_type_template_args(),
				" const_member=", placeholder_func->is_const_member_function(),
				" params=", placeholder_func->parameter_nodes().size(),
				" has_definition=", placeholder_func->get_definition().has_value());
		}
		for (const auto& member_func : struct_info->member_functions) {
			if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}
			const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			FLASH_LOG(ConstExpr, Debug,
				"  candidate name='", func_decl.decl_node().identifier_token().value(),
				"' const=", func_decl.is_const_member_function(),
				" constexpr=", func_decl.is_constexpr(),
				" materialized=", func_decl.is_materialized());
		}
	}
	const FunctionDeclarationNode* actual_func =
		sema_resolved_direct_query.state == ResolvedFunctionQueryResult::State::Available
			? sema_resolved_direct_query.function
			: member_function_match.function;
	const StructMemberFunction* actual_member =
		actual_func ? findMemberFunctionMetadataRecursive(struct_info, *actual_func) : member_function_match.member;
	if (actual_func &&
		receiver_is_const &&
		!actual_func->is_static() &&
		!actual_func->is_const_member_function()) {
		const bool fallback_is_const_compatible =
			member_function_match.function != nullptr &&
			(member_function_match.function->is_static() ||
				member_function_match.function->is_const_member_function());
		if (fallback_is_const_compatible) {
			actual_func = member_function_match.function;
			actual_member = member_function_match.member;
		} else {
			actual_func = nullptr;
			actual_member = nullptr;
		}
	}
	if (!actual_func) {
		actual_func = try_get_lowered_constexpr_member_call_target(
			call_expr,
			struct_info,
			arguments.size(),
			context,
			MemberFunctionLookupMode::LookupOnly,
			false);
		actual_member =
			actual_func ? findMemberFunctionMetadataRecursive(struct_info, *actual_func) : nullptr;
		const bool should_enforce_const_receiver_on_lowered =
			actual_func != nullptr &&
			actual_member != nullptr;
		if (actual_func != nullptr &&
			receiver_is_const &&
			should_enforce_const_receiver_on_lowered &&
			!actual_func->is_static() &&
			!actual_func->is_const_member_function()) {
			actual_func = nullptr;
		}
		actual_member =
			actual_func ? actual_member : nullptr;
		if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
			FLASH_LOG(ConstExpr, Debug,
				"  after try_get_lowered: actual_func=", actual_func != nullptr,
				" actual_member=", actual_member != nullptr);
		}
	}
	if (actual_member && actual_member->function_decl.is<FunctionDeclarationNode>() && actual_func) {
		const auto& member_function_decl = actual_member->function_decl.as<FunctionDeclarationNode>();
		if (!sameConstexprFunctionIdentity(member_function_decl, *actual_func)) {
			actual_member = nullptr;
		}
	}
	// Virtual dispatch: resolve to the final overrider in the dynamic type using
	// is_virtual/is_override flags so that same-name non-overriding derived
	// overloads (e.g. Derived::f(long) vs Base::f(int)) are not selected.
	if (actual_func && actual_member && actual_member->is_virtual) {
		if (const StructMemberFunction* override_member = findFinalOverrider(
				struct_info,
				*actual_member,
				actual_func->parameter_nodes().size())) {
			actual_func = &override_member->function_decl.as<FunctionDeclarationNode>();
			actual_member = override_member;
		}
	}
	if (actual_func) {
		if (const FunctionDeclarationNode* symbol_table_func =
				findMatchingSymbolTableMemberDefinition(*actual_func, context);
			symbol_table_func &&
			sameConstexprFunctionIdentity(*symbol_table_func, *actual_func)) {
			actual_func = symbol_table_func;
		}
	}
	if (actual_func &&
		!actual_func->get_definition().has_value()) {
		const FunctionDeclarationNode* requested_func = actual_func;
		StringHandle owner_name = struct_info->name;
		if (!actual_func->parent_struct_name().empty()) {
			owner_name = StringTable::getOrInternStringHandle(actual_func->parent_struct_name());
		}
		auto replay_sema_services =
			context
				.requireParserAttachedSema(kMemberFunctionMaterializationReplayOp)
				.parserSemanticServices();
		if (std::optional<ASTNode> refreshed =
				replay_sema_services.ensureMemberFunctionMaterialized(owner_name, *actual_func);
			refreshed.has_value() && refreshed->is<FunctionDeclarationNode>()) {
			const auto& refreshed_func = refreshed->as<FunctionDeclarationNode>();
			if (sameConstexprFunctionIdentity(refreshed_func, *requested_func)) {
				actual_func = &refreshed_func;
			}
		}
	}

	if (!actual_func && IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
		FLASH_LOG(ConstExpr, Debug,
			"  after canonical member lookup: actual_func=", actual_func != nullptr,
			" actual_member=", actual_member != nullptr);
	}
	if (!actual_func) {
		return EvalResult::error("Member function not found: " + std::string(func_name));
	}

	// Check if it's a constexpr or consteval function
	if (!actual_func->is_constexpr() && !actual_func->is_consteval()) {
		return EvalResult::error("Member function must be constexpr or consteval: " + std::string(func_name));
	}

	// Get the function body
	const auto& definition = actual_func->get_definition();
	if (!definition.has_value()) {
		return EvalResult::error("Constexpr member function has no body: " + std::string(func_name));
	}

	// Extract member values from the object for 'this' access
	std::unordered_map<std::string_view, EvalResult> member_bindings;
	if (has_complex_object_result) {
		member_bindings = std::move(complex_object_result.object_member_bindings);
	} else {
		auto member_extraction_result = extract_object_members(object_expr, member_bindings, context);
		if (!member_extraction_result.success()) {
			return member_extraction_result;
		}
	}

	// Evaluate function arguments and add to bindings
	const auto& parameters = actual_func->parameter_nodes();
	auto bind_result = bind_evaluated_arguments(
		parameters,
		arguments,
		member_bindings,
		context,
		"Invalid parameter node in constexpr member function");
	if (!bind_result.success()) {
		return bind_result;
	}

	EvalResult this_binding = EvalResult::from_int(0LL);
	this_binding.object_type_index = type_index;
	this_binding.object_member_bindings = member_bindings;
	member_bindings["this"] = std::move(this_binding);

	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto saved_template_environment = context.template_environment;
	auto restore_template_bindings = [&]() {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
		context.template_environment = std::move(saved_template_environment);
	};

	if (actual_func->has_outer_template_bindings()) {
		context.template_param_names.clear();
		context.template_args.clear();
		context.template_param_names.reserve(actual_func->outer_template_param_names().size());
		context.template_args.reserve(actual_func->outer_template_args().size());
		for (StringHandle param_name : actual_func->outer_template_param_names()) {
			context.template_param_names.push_back(StringTable::getStringView(param_name));
		}
		for (const auto& arg : actual_func->outer_template_args()) {
			context.template_args.push_back(toTemplateTypeArg(arg));
		}
		context.template_environment = buildOuterFunctionTemplateEnvironment(
			actual_func->outer_template_param_names(),
			actual_func->outer_template_args());
	} else {
		if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
			load_template_bindings_from_type(type_info, context);
		}
	}
	auto saved_struct_info = context.struct_info;
	auto saved_struct_type_index = context.struct_type_index;
	bool saved_current_member_function_is_const = context.current_member_function_is_const;
	context.struct_info = struct_info;
	context.struct_type_index = type_index;
	context.current_member_function_is_const = actual_func->is_const_member_function();
	// Set return_type_info so that aggregate-initializer returns (return {x, y}) work correctly.
	const TypeInfo* saved_return_type_info = context.return_type_info;
	context.return_type_info = nullptr;
	{
		const TypeSpecifierNode& ret_spec = actual_func->decl_node().type_specifier_node();
		TypeIndex ret_idx = ret_spec.type_index();
		if (const TypeInfo* return_type_info = tryGetTypeInfo(ret_idx))
			context.return_type_info = return_type_info;
	}

	// Increase recursion depth
	context.current_depth++;
	auto* saved_local_bindings = context.local_bindings;
	context.local_bindings = &member_bindings;

	// Evaluate the function body
	auto result = evaluate_block_with_bindings(
		definition.value(),
		member_bindings,
		context,
		"Member function body is not a block",
		"Constexpr member function did not return a value");
	context.local_bindings = saved_local_bindings;
	context.current_depth--;
	context.return_type_info = saved_return_type_info;
	context.struct_info = saved_struct_info;
	context.struct_type_index = saved_struct_type_index;
	context.current_member_function_is_const = saved_current_member_function_is_const;
	restore_template_bindings();
	return result;
}

} // namespace ConstExpr
