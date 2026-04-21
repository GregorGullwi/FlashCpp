#pragma once

#include "AstNodeTypes_Core.h"
#include "AstNodeTypes_Expr.h"
#include "AstNodeTypes_Template.h"

namespace FlashCpp {

template <typename OnStructEnter,
		  typename OnStructExit,
		  typename OnNamespaceEnter,
		  typename OnNamespaceExit,
		  typename OnOtherNode>
void walkReachableStructDecls(
	const ASTNode& root,
	OnStructEnter&& on_struct_enter,
	OnStructExit&& on_struct_exit,
	OnNamespaceEnter&& on_namespace_enter,
	OnNamespaceExit&& on_namespace_exit,
	OnOtherNode&& on_other_node) {
	const auto walk = [&](auto&& self, const ASTNode& node) -> void {
		if (!node.has_value()) {
			return;
		}

		if (node.is<NamespaceDeclarationNode>()) {
			const auto& ns = node.as<NamespaceDeclarationNode>();
			if (!on_namespace_enter(ns)) {
				return;
			}
			for (const auto& child : ns.declarations()) {
				self(self, child);
			}
			on_namespace_exit(ns);
			return;
		}

		if (node.is<StructDeclarationNode>()) {
			const auto& decl = node.as<StructDeclarationNode>();
			if (!on_struct_enter(decl)) {
				return;
			}
			for (const auto& nested : decl.nested_classes()) {
				self(self, nested);
			}
			on_struct_exit(decl);
			return;
		}

		on_other_node(node);
	};

	walk(walk, root);
}

template <typename OnStruct>
void forEachReachableStructDecl(const ASTNode& root, OnStruct&& on_struct) {
	walkReachableStructDecls(
		root,
		[&](const StructDeclarationNode& decl) {
			on_struct(decl);
			return true;
		},
		[](const StructDeclarationNode&) {},
		[](const NamespaceDeclarationNode&) { return true; },
		[](const NamespaceDeclarationNode&) {},
		[](const ASTNode&) {});
}

} // namespace FlashCpp
