#include "CompilerIncludes.h"
#include "CanonicalTypeAdapter.h"
#include "doctest.h"

TEST_SUITE("FrontendContext") {
	TEST_CASE("Scratch budget permits exact-budget typed allocations") {
		struct Value {
			double real;
			int count;
			bool operator==(const Value&) const = default;
		};
		auto check_exact = []<typename T>(T value) {
			DiagnosticEngine diagnostics;
			MonotonicScratchArena arena(diagnostics, sizeof(T));
			T* object = arena.allocateObject<T>(value);
			CHECK(*object == value);
			CHECK(reinterpret_cast<uintptr_t>(object) % alignof(T) == 0);
			CHECK(arena.currentBytes() == sizeof(T));
			CHECK(arena.reservedBytes() == sizeof(T));
			CHECK_FALSE(diagnostics.hasErrors());
		};
		check_exact(uint64_t{42});
		check_exact(short{7});
		check_exact(Value{3.5, 19});
	}

	TEST_CASE("Scratch budget rejects before mutation and permits the exact limit") {
		const uint64_t outside_before = diagnosticsEmittedOutsideEngineCount();
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 16);
		CHECK(arena.allocate(16, 1) != nullptr);
		CHECK(arena.currentBytes() == 16);
		CHECK(arena.reservedBytes() == 16);
		const auto checkpoint = arena.mark();
		const auto peak = arena.peakBytes();
		const auto reserved_peak = arena.peakReservedBytes();
		try {
			arena.allocate(1, 1);
			FAIL("exhausted scratch budget accepted an allocation");
		} catch (const CompileError& error) {
			REQUIRE(error.structuredDiagnostic() != nullptr);
			CHECK(error.structuredDiagnostic()->id == DiagnosticId::ScratchAllocationLimit);
			CHECK(error.structuredDiagnostic()->severity == DiagnosticSeverity::Fatal);
			CHECK(error.structuredDiagnostic()->message_template == "Scratch allocation budget exhausted");
		}
		CHECK(diagnostics.count(DiagnosticSeverity::Fatal) == 1);
		CHECK(arena.mark().block_index == checkpoint.block_index);
		CHECK(arena.mark().block_used == checkpoint.block_used);
		CHECK(arena.mark().destructor_count == checkpoint.destructor_count);
		CHECK(arena.currentBytes() == 16);
		CHECK(arena.reservedBytes() == 16);
		CHECK(arena.discardedBytes() == 0);
		CHECK(arena.peakBytes() == peak);
		CHECK(arena.peakReservedBytes() == reserved_peak);
		CHECK(arena.allocate(0, 1) == nullptr);
		CHECK(diagnostics.count(DiagnosticSeverity::Fatal) == 1);
		CHECK(diagnosticsEmittedOutsideEngineCount() == outside_before);
	}

	TEST_CASE("Scratch budget counts 4096 failed probes without replenishment") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 4096);
		ScratchProbeRegistry registry;
		for (uint32_t index = 0; index < 4096; ++index) {
			ScratchTransaction probe(arena, registry);
			CHECK(probe.registry().registerEntry() == 1);
			probe.arena().allocate(1, 1);
		}
		CHECK(arena.currentBytes() == 0);
		CHECK(arena.discardedBytes() == 4096);
		CHECK(arena.reservedBytes() == 4096);
		CHECK(registry.liveCount() == 0);
		CHECK_THROWS_AS(arena.allocate(1, 1), CompileError);
		CHECK(arena.discardedBytes() == arena.byteLimit());
	}

	TEST_CASE("Scratch budget exhaustion unwinds nested probes exactly once") {
		struct Tracked {
			int* destroyed;
			explicit Tracked(int* count) : destroyed(count) {}
			~Tracked() { ++*destroyed; }
		};
		DiagnosticEngine diagnostics;
		int destroyed = 0;
		MonotonicScratchArena arena(diagnostics, 3 * sizeof(Tracked));
		ScratchProbeRegistry registry;
		registry.registerEntry();
		registry.commitToCurrent();
		arena.allocateObject<Tracked>(&destroyed);
		try {
			ScratchTransaction outer(arena, registry);
			registry.registerEntry();
			arena.allocateObject<Tracked>(&destroyed);
			ScratchTransaction inner(arena, registry);
			registry.registerEntry();
			arena.allocateObject<Tracked>(&destroyed);
			arena.allocateObject<Tracked>(&destroyed);
			FAIL("nested probe exceeded its budget");
		} catch (const CompileError&) {
			CHECK(destroyed == 2);
		}
		CHECK(destroyed == 2);
		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);
		CHECK(registry.registerEntry() == 2);
		CHECK(arena.currentBytes() == sizeof(Tracked));
		CHECK(arena.discardedBytes() == 2 * sizeof(Tracked));
		CHECK(arena.mark().destructor_count == 1);
	}

	TEST_CASE("Scratch budget charges alignment padding and bounds retained blocks") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 8192);
		arena.allocate(1, 1);
		arena.allocate(8, 8);
		CHECK(arena.currentBytes() == 16);
		const auto checkpoint = arena.mark();
		arena.allocate(4096, 1);
		CHECK(arena.reservedBytes() == 8192);
		arena.rollbackTo(checkpoint);
		CHECK(arena.reservedBytes() == 4096);
		CHECK(arena.currentBytes() == 16);
		CHECK(arena.discardedBytes() == 4096);
		CHECK_THROWS_AS(arena.allocate(4096, 1), CompileError);
		CHECK(arena.reservedBytes() == 4096);
		CHECK(arena.peakReservedBytes() == 8192);
		arena.allocate(4080, 1);
		CHECK(arena.currentBytes() + arena.discardedBytes() == arena.byteLimit());
	}

	TEST_CASE("Scratch budget validates zero and oversized requests without allocation") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena empty(diagnostics, 0);
		CHECK(empty.allocate(0, 1) == nullptr);
		CHECK_THROWS_AS(empty.allocate(1, 1), CompileError);
		CHECK(empty.reservedBytes() == 0);
		MonotonicScratchArena arena(diagnostics, 64);
		CHECK_THROWS_AS(arena.allocate(static_cast<std::size_t>(-1), 8), CompileError);
		CHECK_THROWS_AS(arena.allocate(1, std::size_t{1} << (sizeof(std::size_t) * 8 - 1)), CompileError);
		CHECK_THROWS_AS(arena.allocate(1, 0), InternalError);
		CHECK_THROWS_AS(arena.allocate(1, 3), InternalError);
		CHECK(arena.currentBytes() == 0);
		CHECK(arena.reservedBytes() == 0);
		CHECK(arena.peakBytes() == 0);
		CHECK(arena.peakReservedBytes() == 0);
		CHECK(arena.discardedBytes() == 0);
		MonotonicScratchArena overflow(diagnostics, UINT64_MAX);
		CHECK_THROWS_AS(overflow.allocate(static_cast<std::size_t>(-1), 8), CompileError);
		CHECK(overflow.currentBytes() == 0);
		CHECK(overflow.reservedBytes() == 0);
		CHECK(overflow.peakReservedBytes() == 0);
	}

	TEST_CASE("Scratch budget rejects padding and reservation exhaustion independently") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena padded(diagnostics, 16);
		padded.allocate(1, 1);
		const auto padding_checkpoint = padded.mark();
		padded.allocate(14, 1);
		padded.rollbackTo(padding_checkpoint);
		CHECK_THROWS_AS(padded.allocate(1, 8), CompileError);
		CHECK(padded.currentBytes() == 1);
		CHECK(padded.peakBytes() == 15);
		CHECK(padded.reservedBytes() == 16);
		CHECK(padded.discardedBytes() == 14);
		CHECK(padded.mark().block_used == padding_checkpoint.block_used);
		CHECK(padded.allocate(1, 1) != nullptr);

		MonotonicScratchArena fragmented(diagnostics, 8192);
		fragmented.allocate(3000, 1);
		fragmented.allocate(3000, 1);
		REQUIRE(fragmented.currentBytes() == 6000);
		REQUIRE(fragmented.reservedBytes() == 8192);
		const auto checkpoint = fragmented.mark();
		CHECK_THROWS_AS(fragmented.allocate(2000, 1), CompileError);
		CHECK(fragmented.currentBytes() == 6000);
		CHECK(fragmented.peakBytes() == 6000);
		CHECK(fragmented.reservedBytes() == 8192);
		CHECK(fragmented.peakReservedBytes() == 8192);
		CHECK(fragmented.discardedBytes() == 0);
		CHECK(fragmented.mark().block_index == checkpoint.block_index);
		CHECK(fragmented.mark().block_used == checkpoint.block_used);
	}

	TEST_CASE("Scratch budget aligns typed objects by address") {
		struct alignas(256) Aligned { int value; };
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 4096);
		arena.allocate(1, 1);
		const auto before = arena.currentBytes();
		Aligned* object = arena.allocateObject<Aligned>();
		CHECK(reinterpret_cast<uintptr_t>(object) % alignof(Aligned) == 0);
		CHECK(arena.currentBytes() >= before + sizeof(Aligned));
		CHECK(arena.currentBytes() < before + sizeof(Aligned) + alignof(Aligned));
		object->value = 73;
		CHECK(object->value == 73);
	}

	TEST_CASE("Strong semantic IDs reject pointer construction") {
		static_assert(!std::is_default_constructible_v<MonotonicScratchArena>);
		static_assert(!std::is_constructible_v<ScopeId, const void*>);
		static_assert(!std::is_constructible_v<OwnerId, const void*>);
		static_assert(!std::is_constructible_v<DeclId, const void*>);
		static_assert(!std::is_constructible_v<EntityId, const void*>);
		static_assert(!std::is_constructible_v<ExprId, const void*>);
		static_assert(!std::is_constructible_v<TypeId, const void*>);
		static_assert(!std::is_constructible_v<TemplateDeclId, const void*>);
		static_assert(sizeof(ScopeId) == 4);
		static_assert(sizeof(TypeId) == 4);
	}

	TEST_CASE("Scratch probe rollback restores committed registry entries") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;
		const uint32_t committed = registry.registerEntry();
		registry.commitToCurrent();
		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);

		{
			ScratchTransaction probe(arena, registry);
			CHECK(probe.registry().registerEntry() == committed + 1);
			CHECK(registry.liveCount() == 2);
			probe.rollback();
		}

		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);
		CHECK(registry.registerEntry() == committed + 1);
	}

	TEST_CASE("Nested scratch registry transactions restore outer checkpoint") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;

		ScratchTransaction outer(arena, registry);
		CHECK(outer.registry().registerEntry() == 1);

		{
			ScratchTransaction inner(arena, registry);
			CHECK(inner.registry().registerEntry() == 2);
			inner.commit();
		}

		CHECK(registry.liveCount() == 2);
		CHECK(registry.committedCount() == 2);

		outer.rollback();

		CHECK(registry.liveCount() == 0);
		CHECK(registry.committedCount() == 0);
		CHECK(registry.registerEntry() == 1);
	}

	TEST_CASE("Frontend scratch rollback restores symbol and namespace publication") {
		auto make_literal = [](uint64_t value) {
			Token token(Token::Type::Literal, std::string_view("0"), 0, 0, 0);
			return ASTNode::emplace_node<ExpressionNode>(
				NumericLiteralNode(token, value, TypeCategory::Int, TypeQualifier::None, 32));
		};
		SymbolTable table;
		FrontendContext context;
		bindPersistentScopePublication(table);

		const StringHandle existing_namespace_name =
			StringTable::getOrInternStringHandle("frontend_tx_existing_namespace");
		const NamespaceHandle existing_namespace = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, existing_namespace_name);
		const NamespaceHandle inline_child = gNamespaceRegistry.getOrCreateNamespace(
			existing_namespace, StringTable::getOrInternStringHandle("frontend_tx_inline_child"));
		const StringHandle existing_symbol_name =
			StringTable::getOrInternStringHandle("frontend_tx_existing_symbol");
		const StringHandle temporary_symbol_name =
			StringTable::getOrInternStringHandle("frontend_tx_temporary_symbol");
		const StringHandle nested_symbol_name =
			StringTable::getOrInternStringHandle("frontend_tx_nested_symbol");
		const ASTNode existing_symbol = make_literal(1);
		table.insert_into_namespace(existing_namespace, existing_symbol_name, existing_symbol, false);
		const std::size_t namespace_count_before = gNamespaceRegistry.currentSize();
		REQUIRE_FALSE(gNamespaceRegistry.isDeclared(existing_namespace));
		REQUIRE_FALSE(gNamespaceRegistry.isInline(inline_child));

		auto outer = context.beginScratchTransaction();
		gNamespaceRegistry.markDeclared(existing_namespace);
		gNamespaceRegistry.markInline(inline_child);
		table.insert_into_namespace(existing_namespace, existing_symbol_name, make_literal(2), false);
		table.insert_into_namespace(existing_namespace, temporary_symbol_name, make_literal(3), false);
		REQUIRE(table.insert(std::string_view("frontend_tx_global_symbol"), make_literal(4)));
		const NamespaceHandle provisional_namespace = gNamespaceRegistry.getOrCreateNamespace(
			existing_namespace, StringTable::getOrInternStringHandle("frontend_tx_provisional_namespace"));
		CHECK(provisional_namespace.isValid());

		{
			auto inner = context.beginScratchTransaction();
			table.insert_into_namespace(existing_namespace, nested_symbol_name, make_literal(5), false);
			inner.commit();
		}
		const StringHandle nested_context_namespace_name =
			StringTable::getOrInternStringHandle("frontend_tx_nested_context_namespace");
		{
			SymbolTable nested_table;
			FrontendContext nested_context;
			bindPersistentScopePublication(nested_table);
			auto nested_context_transaction = nested_context.beginScratchTransaction();
			const NamespaceHandle nested_context_namespace = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE, nested_context_namespace_name);
			nested_table.enter_namespace(nested_context_namespace);
			REQUIRE(nested_table.insert(std::string_view("nested_context_symbol"), make_literal(6)));
			nested_table.exit_scope();
			nested_context_transaction.commit();
		}
		outer.rollback();

		CHECK(gNamespaceRegistry.currentSize() == namespace_count_before);
		CHECK_FALSE(gNamespaceRegistry.isDeclared(existing_namespace));
		CHECK_FALSE(gNamespaceRegistry.isInline(inline_child));
		CHECK(gNamespaceRegistry.getInlineChildren(existing_namespace).empty());
		CHECK(gNamespaceRegistry.lookupNamespace(
			existing_namespace,
			StringTable::getOrInternStringHandle("frontend_tx_provisional_namespace")).isValid() == false);
		CHECK_FALSE(gNamespaceRegistry.lookupNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, nested_context_namespace_name).isValid());

		const auto existing_symbols = table.lookup_qualified_all(existing_namespace, existing_symbol_name);
		REQUIRE(existing_symbols.size() == 1u);
		CHECK(existing_symbols.front().raw_pointer() == existing_symbol.raw_pointer());
		CHECK_FALSE(table.lookup_qualified(existing_namespace, temporary_symbol_name).has_value());
		CHECK_FALSE(table.lookup_qualified(existing_namespace, nested_symbol_name).has_value());
		CHECK_FALSE(table.lookup("frontend_tx_global_symbol").has_value());
		CHECK_FALSE(table.lookup_qualified(
			NamespaceRegistry::GLOBAL_NAMESPACE,
			StringTable::getOrInternStringHandle("frontend_tx_global_symbol")).has_value());
	}

	TEST_CASE("Frontend scratch commit preserves symbol and namespace publication") {
		Token token(Token::Type::Literal, std::string_view("0"), 0, 0, 0);
		ASTNode symbol = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(token, 9ULL, TypeCategory::Int, TypeQualifier::None, 32));
		SymbolTable table;
		FrontendContext context;
		bindPersistentScopePublication(table);
		const StringHandle namespace_name =
			StringTable::getOrInternStringHandle("frontend_tx_committed_namespace");
		const StringHandle symbol_name =
			StringTable::getOrInternStringHandle("frontend_tx_committed_symbol");

		auto transaction = context.beginScratchTransaction();
		const NamespaceHandle namespace_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, namespace_name);
		table.enter_namespace(namespace_handle);
		REQUIRE(table.insert(StringTable::getStringView(symbol_name), symbol));
		table.exit_scope();
		transaction.commit();

		CHECK(gNamespaceRegistry.lookupNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, namespace_name) == namespace_handle);
		CHECK(gNamespaceRegistry.isDeclared(namespace_handle));
		CHECK(table.lookup_qualified(namespace_handle, symbol_name).has_value());
	}

	TEST_CASE("Scratch arena survives allocations larger than one block") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		const std::size_t first_size = 5000U;
		const std::size_t second_size = 16U;
		void* first = arena.allocate(first_size, alignof(std::max_align_t));
		void* second = arena.allocate(second_size, alignof(std::max_align_t));
		CHECK(first != nullptr);
		CHECK(second != nullptr);
		CHECK(arena.currentBytes() >= first_size + second_size);
		CHECK(arena.reservedBytes() >= 4096U);
		CHECK(arena.reservedBytes() >= arena.currentBytes());

		const ScratchArenaState checkpoint = arena.mark();
		arena.allocate(32U, alignof(std::max_align_t));
		arena.rollbackTo(checkpoint);
		CHECK(arena.currentBytes() >= first_size + second_size);
	}

	TEST_CASE("Scratch arena byte accounting includes alignment padding on rollback") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		CHECK(arena.allocate(1U, 1U) != nullptr);
		const uint64_t bytes_after_first = arena.currentBytes();
		CHECK(arena.allocate(1U, 8U) != nullptr);
		const uint64_t bytes_after_second = arena.currentBytes();
		CHECK(bytes_after_second > bytes_after_first);

		const ScratchArenaState checkpoint = arena.mark();
		arena.allocate(4U, 8U);
		arena.rollbackTo(checkpoint);
		CHECK(arena.currentBytes() == bytes_after_second);
	}

	TEST_CASE("Committed scratch objects are destroyed at arena teardown") {
		struct ScratchDestructionProbe {
			bool* destroyed_flag;
			explicit ScratchDestructionProbe(bool* destroyed_flag_in)
				: destroyed_flag(destroyed_flag_in) {
			}
			~ScratchDestructionProbe() {
				if (destroyed_flag != nullptr) {
					*destroyed_flag = true;
				}
			}
		};

		bool destroyed = false;
		{
			DiagnosticEngine diagnostics;
			MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
			ScratchProbeRegistry registry;
			ScratchTransaction probe(arena, registry);
			probe.arena().allocateObject<ScratchDestructionProbe>(&destroyed);
			probe.commit();
			CHECK(!destroyed);
		}
		CHECK(destroyed);
	}

	TEST_CASE("Scratch probe commit publishes registry entries") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;

		{
			ScratchTransaction probe(arena, registry);
			CHECK(probe.registry().registerEntry() == 1);
			probe.commit();
		}

		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);
	}

	TEST_CASE("Scratch probe rollback runs destructors and discards bytes") {
		struct ScratchDestructionProbe {
			bool* destroyed_flag;
			explicit ScratchDestructionProbe(bool* destroyed_flag_in)
				: destroyed_flag(destroyed_flag_in) {
			}
			~ScratchDestructionProbe() {
				if (destroyed_flag != nullptr) {
					*destroyed_flag = true;
				}
			}
		};

		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;
		bool destroyed = false;
		const uint64_t bytes_before = arena.currentBytes();

		{
			ScratchTransaction probe(arena, registry);
			probe.arena().allocateObject<ScratchDestructionProbe>(&destroyed);
			CHECK(arena.currentBytes() > bytes_before);
			probe.rollback();
		}

		CHECK(destroyed);
		CHECK(arena.currentBytes() == bytes_before);
		CHECK(arena.discardedBytes() > 0);
	}

	TEST_CASE("FrontendContext owns canonical type storage and accounts for its bytes") {
		FrontendContext context;
		CHECK(context.canonicalTypes().size() == 0);
		const TypeId integer = context.canonicalTypes().builtin(CanonicalBuiltinKind::Int);
		const TypeId pointer = context.canonicalTypes().pointer(integer);
		context.refreshSemanticDomainStats();
		const auto stats = context.canonicalTypes().arenaStats();
		CHECK(stats.used_bytes == 2 * sizeof(CanonicalTypeNode));
		CHECK(context.domainStats(AllocationDomain::Semantic).current_bytes == stats.used_bytes);
		CHECK(context.domainStats(AllocationDomain::Semantic).reserved_bytes == stats.reserved_bytes);
		{
			FrontendContext other;
			CHECK(other.canonicalTypes().size() == 0);
			other.canonicalTypes().builtin(CanonicalBuiltinKind::Double);
			CHECK(other.canonicalTypes().size() == 1);
		}
		CHECK(context.canonicalTypes().pointer(integer) == pointer);
		CHECK(context.canonicalTypes().size() == 2);
	}

	TEST_CASE("FrontendContext exposes active context and scratch telemetry") {
		FrontendContext outer;
		CHECK(FrontendContext::active() == &outer);
		{
			FrontendContext inner;
			CHECK(FrontendContext::active() == &inner);
		}
		CHECK(FrontendContext::active() == &outer);

		FrontendContext context;
		CHECK(&context.scratchArena() == &context.scratchArena());
		CHECK(context.scratchArena().byteLimit() == 64ULL * 1024 * 1024);
		CHECK_THROWS_AS(context.scratchArena().allocate(64ULL * 1024 * 1024 + 1, 1), CompileError);
		CHECK(context.diagnostics().count(DiagnosticSeverity::Fatal) == 1);
#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
		const uint64_t spills_before = FlashCpp::inlineVectorSpillCount();
		const uint64_t overload_spills_before =
			FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::OverloadResolution);
		const uint64_t template_spills_before =
			FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::TemplateArgument);
		FlashCpp::OverloadVector<int, 2> overload_values;
		overload_values.push_back(1);
		overload_values.push_back(2);
		overload_values.push_back(3);
		FlashCpp::TemplateVector<TemplateTypeArg, 2>
			template_values;
		template_values.push_back(TemplateTypeArg{});
		template_values.push_back(TemplateTypeArg{});
		template_values.push_back(TemplateTypeArg{});
		CHECK(FlashCpp::inlineVectorSpillCount() == spills_before + 2);
		CHECK(context.inlineVectorSpillCount() == spills_before + 2);
		CHECK(FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::OverloadResolution) ==
			  overload_spills_before + 1);
		CHECK(FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::TemplateArgument) ==
			  template_spills_before + 1);
#endif
		context.refreshScratchDomainStats();
		const DomainByteStats scratch_stats = context.domainStats(AllocationDomain::Scratch);
		CHECK(scratch_stats.current_bytes == context.scratchArena().currentBytes());
		CHECK(scratch_stats.peak_bytes == context.scratchArena().peakBytes());
		CHECK(scratch_stats.reserved_bytes == context.scratchArena().reservedBytes());
		CHECK(scratch_stats.peak_reserved_bytes == context.scratchArena().peakReservedBytes());
	}

	template<typename T>
	concept StoresDuplicateScopeId = requires(T scope) { scope.scope_id; };
	static_assert(!StoresDuplicateScopeId<Scope>);

	TEST_CASE("Scope slot identities survive deep exit and sibling publication") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		constexpr uint32_t depth = 4096;
		for (uint32_t index = 0; index < depth; ++index) {
			table.enter_scope(ScopeType::Block);
			REQUIRE(table.currentScopeId().value == index + 2);
			CHECK(context.currentScopeId() == table.currentScopeId());
		}
		for (uint32_t index = depth; index > 0; --index) {
			table.exit_scope();
			CHECK(table.currentScopeId().value == index);
		}
		table.enter_scope(ScopeType::Function);
		const ScopeId sibling_id = table.currentScopeId();
		CHECK(sibling_id.value == depth + 2);
		CHECK(context.scopeRecord(sibling_id).parent_id == ScopeId{1});
		CHECK(readScopeMetadata(table, ScopeId{2}).scope_type == ScopeType::Block);
		CHECK(readScopeMetadata(table, sibling_id).scope_type == ScopeType::Function);
		CHECK(table.findScopeById(ScopeId{}) == nullptr);
		CHECK(table.findScopeById(ScopeId{depth + 3}) == nullptr);
		CHECK_THROWS_AS(table.scopeById(ScopeId{}), InternalError);
		CHECK_THROWS_AS(table.scopeById(ScopeId{depth + 3}), InternalError);
		table.clear();
		CHECK(table.currentScopeId() == ScopeId{1});
		CHECK(context.currentScopeId() == ScopeId{1});
		CHECK(table.findScopeById(sibling_id) == nullptr);
		CHECK(table.scopeCount() == 1);
		CHECK(context.scopeCount() == 1);
	}

	TEST_CASE("SymbolTable scope exit moves cursor without destroying scope records") {
		SymbolTable table;
		const ScopeId global_id = table.currentScopeId();
		REQUIRE(global_id.value == 1u);
		REQUIRE(table.scopeCount() == 1u);
		REQUIRE(table.activeScopeDepth() == 1u);

		table.enter_scope(ScopeType::Block);
		const ScopeId block_id = table.currentScopeId();
		REQUIRE(block_id.value == 2u);
		REQUIRE(table.scopeCount() == 2u);
		REQUIRE(table.activeScopeDepth() == 2u);

		table.exit_scope();
		CHECK(table.currentScopeId() == global_id);
		CHECK(table.scopeCount() == 2u);
		CHECK(table.activeScopeDepth() == 1u);
	}

	TEST_CASE("SymbolTable records ScopeId on lookup sites") {
		SymbolTable table;
		table.enter_scope(ScopeType::Namespace);
		const ScopeId namespace_scope_id = table.currentScopeId();
		(void)table.lookup("missing_identifier");
		CHECK(table.lastLookupScopeId() == namespace_scope_id);
		table.exit_scope();
		CHECK(table.lastLookupScopeId() == namespace_scope_id);
	}

	TEST_CASE("get_current_using_declaration_handles prefers inner using-declarations") {
		SymbolTable table;
		const StringHandle ns_a_name = StringTable::getOrInternStringHandle("ReviewUsingA");
		const StringHandle ns_b_name = StringTable::getOrInternStringHandle("ReviewUsingB");
		NamespaceHandle ns_a = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_a_name);
		NamespaceHandle ns_b = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_b_name);

		table.enter_scope(ScopeType::Function);
		table.add_using_declaration("value", ns_a, "value");
		table.enter_scope(ScopeType::Block);
		table.add_using_declaration("value", ns_b, "value");

		const auto handles = table.get_current_using_declaration_handles();
		const auto it = handles.find("value");
		REQUIRE(it != handles.end());
		CHECK(it->second.first == ns_b);
	}

	TEST_CASE("insertGlobal records global ScopeId as declaring scope") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		Token token(Token::Type::Literal, std::string_view("0"), 0, 0, 0);
		ASTNode node = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(token, 0ULL, TypeCategory::Int, TypeQualifier::None, 32));
		REQUIRE(table.insertGlobal("global_var", node));
		CHECK(table.lastDeclaringScopeId().value == 1u);
	}

	TEST_CASE("ASTNode get_if returns active node type or nullptr") {
		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("get_if_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode decl = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		CHECK(decl.get_if<DeclarationNode>() != nullptr);
		CHECK(decl.get_if<FunctionDeclarationNode>() == nullptr);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on DeclarationNode") {
		SymbolTable table;
		table.enter_scope(ScopeType::Function);
		const ScopeId function_scope = table.currentScopeId();
		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("scope_stamp_var"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(table.insert(std::string_view("scope_stamp_var"), node));
		const std::vector<ASTNode> symbols = table.lookup_all("scope_stamp_var");
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<DeclarationNode>().lexical_scope_id() == function_scope);
	}

	TEST_CASE("SymbolTable insert stamps distinct lexical ScopeIds across nested scopes") {
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token global_id(Token::Type::Identifier, std::string_view("scope_stamp_global_var"), 1, 1, 0);
		Token block_id(Token::Type::Identifier, std::string_view("scope_stamp_block_var"), 2, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);

		ASTNode global_node = ASTNode::emplace_node<DeclarationNode>(int_type, global_id);
		REQUIRE(table.insert(std::string_view("scope_stamp_global_var"), global_node));
		CHECK(table.lookup_all(std::string_view("scope_stamp_global_var"))[0]
				  .as<DeclarationNode>()
				  .lexical_scope_id() == global_scope);

		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope = table.currentScopeId();
		REQUIRE(global_scope != block_scope);

		ASTNode block_node = ASTNode::emplace_node<DeclarationNode>(int_type, block_id);
		REQUIRE(table.insert(std::string_view("scope_stamp_block_var"), block_node));
		CHECK(table.lookup_all(std::string_view("scope_stamp_block_var"))[0]
				  .as<DeclarationNode>()
				  .lexical_scope_id() == block_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed FunctionDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "void scope_stamp_parsed_fn();";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_fn_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle fn_name = StringTable::getOrInternStringHandle("scope_stamp_parsed_fn");
		const std::vector<ASTNode> overloads =
			gSymbolTable.lookup_all(StringTable::getStringView(fn_name));
		REQUIRE(overloads.size() == 1u);
		CHECK(overloads[0]
				  .as<FunctionDeclarationNode>()
				  .decl_node()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed TemplateFunctionDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "template<class T> void scope_stamp_tmpl_fn(T);";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_tmpl_fn_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle fn_name = StringTable::getOrInternStringHandle("scope_stamp_tmpl_fn");
		const std::vector<ASTNode> overloads =
			gSymbolTable.lookup_all(StringTable::getStringView(fn_name));
		REQUIRE(overloads.size() == 1u);
		REQUIRE(overloads[0].is<TemplateFunctionDeclarationNode>());
		CHECK(overloads[0]
				  .as<TemplateFunctionDeclarationNode>()
				  .function_decl_node()
				  .decl_node()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("Member class template under published class publishes TemplateDeclId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct Outer {\n"
			"  template<typename T> struct Box;\n"
			"  template<typename T> struct Box { T value; };\n"
			"};\n"
			"template<typename T> struct FreeBox { T value; };\n";
		CompileContext test_context;
		test_context.setInputFile("member_class_template_decl_publication_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle qualified_box =
			StringTable::getOrInternStringHandle("Outer::Box");
		const auto member_opt = gTemplateRegistry.lookupTemplate(qualified_box);
		REQUIRE(member_opt.has_value());
		REQUIRE(member_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& member =
			member_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(member.has_template_decl_id());
		REQUIRE(member.class_decl_node().has_template_decl_id());
		CHECK(member.template_decl_id() == member.class_decl_node().template_decl_id());

		const auto& box_members = member.class_decl_node().members();
		REQUIRE(box_members.size() == 1u);
		REQUIRE(box_members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& value_type =
			box_members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(value_type.has_template_parameter_decl());
		CHECK(value_type.template_decl_id() == member.template_decl_id());
		CHECK(value_type.template_parameter_index() == 0u);

		const StringHandle free_box = StringTable::getOrInternStringHandle("FreeBox");
		const auto free_opt = gTemplateRegistry.lookupTemplate(free_box);
		REQUIRE(free_opt.has_value());
		REQUIRE(free_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& free_box_tmpl =
			free_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(free_box_tmpl.has_template_decl_id());
		CHECK(free_box_tmpl.template_decl_id() != member.template_decl_id());
	}

	TEST_CASE("Member class template under published class template publishes TemplateDeclId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename Outer> struct TemplateOwner {\n"
			"  template<typename Inner> struct Box;\n"
			"  template<typename Inner> struct Box { Inner value; };\n"
			"  template<typename Left, typename Right> struct Pair { Right value; };\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("class_template_member_primary_decl_publication_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto owner_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("TemplateOwner"));
		REQUIRE(owner_opt.has_value());
		REQUIRE(owner_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& owner = owner_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(owner.has_template_decl_id());

		const auto box_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("TemplateOwner::Box"));
		REQUIRE(box_opt.has_value());
		REQUIRE(box_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& box = box_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(box.has_template_decl_id());
		REQUIRE(box.class_decl_node().has_template_decl_id());
		CHECK(box.template_decl_id() == box.class_decl_node().template_decl_id());
		CHECK(box.template_decl_id() != owner.template_decl_id());

		const auto& box_members = box.class_decl_node().members();
		REQUIRE(box_members.size() == 1u);
		REQUIRE(box_members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& value_type =
			box_members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(value_type.has_template_parameter_decl());
		CHECK(value_type.template_decl_id() == box.template_decl_id());
		CHECK(value_type.template_parameter_index() == 0u);

		const auto pair_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("TemplateOwner::Pair"));
		REQUIRE(pair_opt.has_value());
		REQUIRE(pair_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& pair = pair_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(pair.has_template_decl_id());
		CHECK(pair.template_decl_id() != box.template_decl_id());
		CHECK(pair.template_decl_id() != owner.template_decl_id());

		CanonicalTypeTable& table = context.canonicalTypes();
		const CanonicalTypeImport value_import = importCanonicalType(table, value_type);
		REQUIRE(value_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(value_import.type == table.templateParameter(box.template_decl_id(), 0u));
	}

	TEST_CASE("Nested class in a class template publishes under template identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename OwnerValue> struct TemplateOwner {\n"
			"  struct Inner {\n"
			"    template<typename Value> struct Box { Value value; };\n"
			"  };\n"
			"};\n"
			"template<typename OwnerValue> struct OtherTemplateOwner {\n"
			"  struct Inner { int other; };\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_class_in_class_template_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto owner_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("TemplateOwner"));
		REQUIRE(owner_opt.has_value());
		REQUIRE(owner_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& owner = owner_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(owner.has_template_decl_id());
		REQUIRE(owner.class_decl_node().nested_classes().size() == 1u);
		REQUIRE(owner.class_decl_node().nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& inner =
			owner.class_decl_node().nested_classes()[0].as<StructDeclarationNode>();
		REQUIRE(inner.has_entity_id());

		const auto other_owner_opt = gTemplateRegistry.lookupTemplate(
			StringTable::getOrInternStringHandle("OtherTemplateOwner"));
		REQUIRE(other_owner_opt.has_value());
		REQUIRE(other_owner_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& other_owner =
			other_owner_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(other_owner.has_template_decl_id());
		REQUIRE(other_owner.class_decl_node().nested_classes().size() == 1u);
		REQUIRE(other_owner.class_decl_node().nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& other_inner =
			other_owner.class_decl_node().nested_classes()[0].as<StructDeclarationNode>();
		REQUIRE(other_inner.has_entity_id());
		CHECK(inner.entity_id() != other_inner.entity_id());

		const EntityRecord& inner_record = context.declarationBuilder().entity(inner.entity_id());
		CHECK(inner_record.owner_id == ownerIdFromTemplateDecl(owner.template_decl_id()));

		const auto box_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Inner::Box"));
		REQUIRE(box_opt.has_value());
		REQUIRE(box_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& box = box_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(box.has_template_decl_id());
		const auto published_box = context.templateDecls().findPrimaryClassTemplate(
			ownerIdFromClassEntity(inner.entity_id()),
			StringTable::getOrInternStringHandle("Box"));
		REQUIRE(published_box.has_value());
		CHECK(*published_box == box.template_decl_id());

		const auto& box_members = box.class_decl_node().members();
		REQUIRE(box_members.size() == 1u);
		REQUIRE(box_members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& value_type =
			box_members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(value_type.has_template_parameter_decl());
		CHECK(value_type.template_decl_id() == box.template_decl_id());
		CHECK(value_type.template_parameter_index() == 0u);
	}

	TEST_CASE("Nested member class template stamps Spec-rooted dependent members by owner") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename RootValue> struct Root {\n"
			"  template<typename Value> struct Rebind { using type = Value; };\n"
			"};\n"
			"template<typename Outer> struct TemplateOwner {\n"
			"  template<typename Inner> struct Box {\n"
			"    typename Root<Inner>::template Rebind<Inner>::type inner_value;\n"
			"  };\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_member_template_spec_root_stamp_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto owner_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("TemplateOwner"));
		REQUIRE(owner_opt.has_value());
		REQUIRE(owner_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& owner = owner_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(owner.has_template_decl_id());

		const auto box_opt = gTemplateRegistry.lookupTemplate(
			StringTable::getOrInternStringHandle("TemplateOwner::Box"));
		REQUIRE(box_opt.has_value());
		REQUIRE(box_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& box = box_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(box.has_template_decl_id());

		const auto& box_members = box.class_decl_node().members();
		REQUIRE(box_members.size() == 1u);
		REQUIRE(box_members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& inner_type =
			box_members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(inner_type.has_dependent_name_type());

		CanonicalTypeTable& table = context.canonicalTypes();
		const auto root_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Root"));
		REQUIRE(root_opt.has_value());
		REQUIRE(root_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& root = root_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(root.has_template_decl_id());
		const TypeId inner_parameter = table.templateParameter(box.template_decl_id(), 0u);
		const std::array<TypeId, 1> owner_args = {inner_parameter};
		const TypeId owner_spec = table.templateSpecialization(
			root.template_decl_id(), std::span<const TypeId>(owner_args));
		const std::array<TypeId, 1> inner_rebind_args = {inner_parameter};
		const TypeId inner_rebind = table.dependentTemplateMember(
			owner_spec, "Rebind", std::span<const TypeId>(inner_rebind_args));
		const TypeId inner_tip = inner_type.dependent_name_type();
		CHECK(table.node(inner_tip).kind == CanonicalTypeKind::DependentName);
		CHECK(table.node(table.node(inner_tip).child).kind == CanonicalTypeKind::DependentTemplateMember);
		CHECK(table.dependentNameIdentifier(table.node(inner_tip).child) == "Rebind");
		const TypeId inner_actual_owner = table.node(table.node(inner_tip).child).child;
		CHECK(table.node(inner_actual_owner).kind == CanonicalTypeKind::TemplateSpecialization);
		CHECK(table.node(inner_actual_owner).array_extent == table.node(owner_spec).array_extent);
		CHECK(table.templateArgumentType(table.templateSpecializationArguments(inner_actual_owner)) == inner_parameter);
		CHECK(table.node(table.node(inner_tip).child).child == owner_spec);
		CHECK(table.node(inner_tip).child == inner_rebind);
		CHECK(inner_type.dependent_name_type() == table.dependentName(inner_rebind, "type"));
	}

	TEST_CASE("Nested-class member template stamps Spec-rooted dependent members") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename RootValue> struct Root {\n"
			"  template<typename Value> struct Rebind { using type = Value; };\n"
			"};\n"
			"template<typename Owner> struct TemplateOwner {\n"
			"  struct Inner {\n"
			"    template<typename Value> struct Box {\n"
			"      typename Root<Value>::template Rebind<Value>::type value;\n"
			"    };\n"
			"  };\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_class_member_template_spec_root_stamp_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto owner_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("TemplateOwner"));
		REQUIRE(owner_opt.has_value());
		REQUIRE(owner_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& owner = owner_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(owner.has_template_decl_id());
		REQUIRE(owner.class_decl_node().nested_classes().size() == 1u);
		REQUIRE(owner.class_decl_node().nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& inner =
			owner.class_decl_node().nested_classes()[0].as<StructDeclarationNode>();
		REQUIRE(inner.has_entity_id());
		CHECK(context.declarationBuilder().entity(inner.entity_id()).owner_id ==
			  ownerIdFromTemplateDecl(owner.template_decl_id()));

		const auto box_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Inner::Box"));
		REQUIRE(box_opt.has_value());
		REQUIRE(box_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& box = box_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(box.has_template_decl_id());
		CHECK(context.templateDecls().findPrimaryClassTemplate(
			  ownerIdFromClassEntity(inner.entity_id()),
			  StringTable::getOrInternStringHandle("Box")) == box.template_decl_id());

		const auto& box_members = box.class_decl_node().members();
		REQUIRE(box_members.size() == 1u);
		REQUIRE(box_members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& value_type =
			box_members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(value_type.has_dependent_name_type());

		const auto root_opt = gTemplateRegistry.lookupTemplate(
			StringTable::getOrInternStringHandle("Root"));
		REQUIRE(root_opt.has_value());
		REQUIRE(root_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& root = root_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(root.has_template_decl_id());

		CanonicalTypeTable& table = context.canonicalTypes();
		const TypeId value_parameter = table.templateParameter(box.template_decl_id(), 0u);
		const std::array<TypeId, 1> root_args = {value_parameter};
		const TypeId root_spec = table.templateSpecialization(
			root.template_decl_id(), std::span<const TypeId>(root_args));
		const std::array<TypeId, 1> rebind_args = {value_parameter};
		const TypeId rebind = table.dependentTemplateMember(
			root_spec, "Rebind", std::span<const TypeId>(rebind_args));
		const TypeId value_type_id = value_type.dependent_name_type();
		REQUIRE(table.node(value_type_id).kind == CanonicalTypeKind::DependentName);
		const TypeId member_template_id = table.node(value_type_id).child;
		REQUIRE(table.node(member_template_id).kind == CanonicalTypeKind::DependentTemplateMember);
		CHECK(table.dependentNameIdentifier(member_template_id) == "Rebind");
		CHECK(table.node(member_template_id).child == root_spec);
		CHECK(member_template_id == rebind);
		CHECK(value_type_id == table.dependentName(rebind, "type"));
	}

	TEST_CASE("Member function template under published class template publishes TemplateDeclId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename Outer> struct TemplateFunctionOwner {\n"
			"  template<typename Left, typename Right> Right select(Left left, Right right);\n"
			"  template<typename Left, typename Right> Right select(Left left, Right right) { return right; }\n"
			"  template<typename T> T* address(T* value) { return value; }\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("class_template_member_function_decl_publication_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto owner_opt = gTemplateRegistry.lookupTemplate(
			StringTable::getOrInternStringHandle("TemplateFunctionOwner"));
		REQUIRE(owner_opt.has_value());
		REQUIRE(owner_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& owner = owner_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(owner.has_template_decl_id());

		const TemplateFunctionDeclarationNode* declaration = nullptr;
		const TemplateFunctionDeclarationNode* definition = nullptr;
		const TemplateFunctionDeclarationNode* address = nullptr;
		for (const StructMemberFunctionDecl& member : owner.class_decl_node().member_functions()) {
			if (!member.function_declaration.is<TemplateFunctionDeclarationNode>()) {
				continue;
			}
			const TemplateFunctionDeclarationNode& candidate =
				member.function_declaration.as<TemplateFunctionDeclarationNode>();
			const StringHandle name =
				candidate.function_decl_node().decl_node().identifier_token().handle();
			if (StringTable::getStringView(name) == "select"sv) {
				if (candidate.function_decl_node().has_template_body_position()) {
					definition = &candidate;
				} else {
					declaration = &candidate;
				}
			} else if (StringTable::getStringView(name) == "address"sv) {
				address = &candidate;
			}
		}
		REQUIRE(declaration != nullptr);
		REQUIRE(definition != nullptr);
		REQUIRE(address != nullptr);
		REQUIRE(declaration->has_template_decl_id());
		REQUIRE(definition->has_template_decl_id());
		REQUIRE(address->has_template_decl_id());
		CHECK(declaration->template_decl_id() == definition->template_decl_id());
		CHECK(address->template_decl_id() != definition->template_decl_id());
		CHECK(definition->template_decl_id() != owner.template_decl_id());

		const FunctionDeclarationNode& selected = definition->function_decl_node();
		const TypeSpecifierNode& return_type = selected.decl_node().type_specifier_node();
		REQUIRE(return_type.has_template_parameter_decl());
		CHECK(return_type.template_decl_id() == definition->template_decl_id());
		CHECK(return_type.template_parameter_index() == 1u);
		REQUIRE(selected.parameter_nodes().size() == 2u);
		const TypeSpecifierNode& first_param =
			selected.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node();
		const TypeSpecifierNode& second_param =
			selected.parameter_nodes()[1].as<DeclarationNode>().type_specifier_node();
		REQUIRE(first_param.has_template_parameter_decl());
		REQUIRE(second_param.has_template_parameter_decl());
		CHECK(first_param.template_decl_id() == definition->template_decl_id());
		CHECK(first_param.template_parameter_index() == 0u);
		CHECK(second_param.template_decl_id() == definition->template_decl_id());
		CHECK(second_param.template_parameter_index() == 1u);

		CanonicalTypeTable& table = context.canonicalTypes();
		const CanonicalTypeImport return_import = importCanonicalType(table, return_type);
		REQUIRE(return_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(return_import.type == table.templateParameter(definition->template_decl_id(), 1u));
		const CanonicalTypeImport first_import = importCanonicalType(table, first_param);
		REQUIRE(first_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(first_import.type == table.templateParameter(definition->template_decl_id(), 0u));
	}

	TEST_CASE("Free function template overloads publish distinct TemplateDeclIds") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename T> T identity_fn(T value);\n"
			"template<typename T> T identity_fn(T value) { return value; }\n"
			"template<typename T> void overloaded_fn(T);\n"
			"template<typename T> void overloaded_fn(T*);\n"
			"template<typename T> struct IdentityClass { T value; };\n";
		CompileContext test_context;
		test_context.setInputFile("function_template_decl_publication_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle identity_name =
			StringTable::getOrInternStringHandle("identity_fn");
		const auto identity_opt = gTemplateRegistry.lookupTemplate(identity_name);
		REQUIRE(identity_opt.has_value());
		REQUIRE(identity_opt->is<TemplateFunctionDeclarationNode>());
		const TemplateFunctionDeclarationNode& identity =
			identity_opt->as<TemplateFunctionDeclarationNode>();
		REQUIRE(identity.has_template_decl_id());

		const StringHandle class_name =
			StringTable::getOrInternStringHandle("IdentityClass");
		const auto class_opt = gTemplateRegistry.lookupTemplate(class_name);
		REQUIRE(class_opt.has_value());
		REQUIRE(class_opt->is<TemplateClassDeclarationNode>());
		REQUIRE(class_opt->as<TemplateClassDeclarationNode>().has_template_decl_id());
		CHECK(class_opt->as<TemplateClassDeclarationNode>().template_decl_id() !=
			  identity.template_decl_id());

		const StringHandle overloaded_name =
			StringTable::getOrInternStringHandle("overloaded_fn");
		const std::vector<ASTNode>* overloaded =
			gTemplateRegistry.lookupAllTemplates(overloaded_name);
		REQUIRE(overloaded != nullptr);
		std::optional<TemplateDeclId> first_overload_id;
		std::optional<TemplateDeclId> second_overload_id;
		size_t function_count = 0;
		for (const ASTNode& entry : *overloaded) {
			if (!entry.is<TemplateFunctionDeclarationNode>()) {
				continue;
			}
			++function_count;
			const TemplateFunctionDeclarationNode& overload =
				entry.as<TemplateFunctionDeclarationNode>();
			REQUIRE(overload.has_template_decl_id());
			if (!first_overload_id.has_value()) {
				first_overload_id = overload.template_decl_id();
			} else {
				second_overload_id = overload.template_decl_id();
			}
		}
		REQUIRE(function_count == 2u);
		REQUIRE(first_overload_id.has_value());
		REQUIRE(second_overload_id.has_value());
		CHECK(*first_overload_id != *second_overload_id);
		CHECK(*first_overload_id != identity.template_decl_id());
		CHECK(*second_overload_id != identity.template_decl_id());
	}

	TEST_CASE("Free function template stamps its declared type parameters") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename T, typename U> U pick_pair(T left, U right);\n"
			"template<typename T> T* take_ptr(T* value) { return value; }\n";
		CompileContext test_context;
		test_context.setInputFile("function_template_param_stamp_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle pair_name =
			StringTable::getOrInternStringHandle("pick_pair");
		const auto pair_opt = gTemplateRegistry.lookupTemplate(pair_name);
		REQUIRE(pair_opt.has_value());
		REQUIRE(pair_opt->is<TemplateFunctionDeclarationNode>());
		const TemplateFunctionDeclarationNode& pair_template =
			pair_opt->as<TemplateFunctionDeclarationNode>();
		REQUIRE(pair_template.has_template_decl_id());
		const TemplateDeclId pair_decl_id = pair_template.template_decl_id();

		const FunctionDeclarationNode& pair_function =
			pair_template.function_decl_node();
		const TypeSpecifierNode& pair_return =
			pair_function.decl_node().type_specifier_node();
		REQUIRE(pair_return.has_template_parameter_identity());
		REQUIRE(pair_return.has_template_parameter_decl());
		CHECK(pair_return.template_decl_id() == pair_decl_id);
		CHECK(pair_return.template_parameter_index() == 1u);
		REQUIRE(pair_function.parameter_nodes().size() == 2u);
		const TypeSpecifierNode& left_param =
			pair_function.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node();
		REQUIRE(left_param.has_template_parameter_decl());
		CHECK(left_param.template_decl_id() == pair_decl_id);
		CHECK(left_param.template_parameter_index() == 0u);
		const TypeSpecifierNode& right_param =
			pair_function.parameter_nodes()[1].as<DeclarationNode>().type_specifier_node();
		REQUIRE(right_param.has_template_parameter_decl());
		CHECK(right_param.template_decl_id() == pair_decl_id);
		CHECK(right_param.template_parameter_index() == 1u);

		const StringHandle ptr_name =
			StringTable::getOrInternStringHandle("take_ptr");
		const auto ptr_opt = gTemplateRegistry.lookupTemplate(ptr_name);
		REQUIRE(ptr_opt.has_value());
		REQUIRE(ptr_opt->is<TemplateFunctionDeclarationNode>());
		const TemplateFunctionDeclarationNode& ptr_template =
			ptr_opt->as<TemplateFunctionDeclarationNode>();
		REQUIRE(ptr_template.has_template_decl_id());
		const TemplateDeclId ptr_decl_id = ptr_template.template_decl_id();
		CHECK(ptr_decl_id != pair_decl_id);

		const FunctionDeclarationNode& ptr_function =
			ptr_template.function_decl_node();
		const TypeSpecifierNode& ptr_return =
			ptr_function.decl_node().type_specifier_node();
		REQUIRE(ptr_return.has_template_parameter_decl());
		CHECK(ptr_return.template_decl_id() == ptr_decl_id);
		CHECK(ptr_return.template_parameter_index() == 0u);
		REQUIRE(ptr_function.parameter_nodes().size() == 1u);
		const TypeSpecifierNode& ptr_param =
			ptr_function.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node();
		REQUIRE(ptr_param.has_template_parameter_decl());
		CHECK(ptr_param.template_decl_id() == ptr_decl_id);
		CHECK(ptr_param.template_parameter_index() == 0u);

		FrontendContext* front_end = FrontendContext::active();
		REQUIRE(front_end != nullptr);
		CanonicalTypeTable& table = front_end->canonicalTypes();
		const CanonicalTypeImport return_import = importCanonicalType(table, pair_return);
		REQUIRE(return_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(return_import.type == table.templateParameter(pair_decl_id, 1u));
		const CanonicalTypeImport left_import = importCanonicalType(table, left_param);
		REQUIRE(left_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(left_import.type == table.templateParameter(pair_decl_id, 0u));
		const CanonicalTypeImport ptr_param_import = importCanonicalType(table, ptr_param);
		REQUIRE(ptr_param_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(ptr_param_import.type == table.pointer(table.templateParameter(ptr_decl_id, 0u)));
	}

	TEST_CASE("Published member function templates stamp their declared type parameters") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct MemberTemplateOwner {\n"
			"  template<typename T, typename U> U select(T left, U value);\n"
			"  template<typename T, typename U> U select(T left, U value) { return value; }\n"
			"  template<typename T> T* address(T* value) { return value; }\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_function_template_decl_publication_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StructDeclarationNode* owner = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (node.is<StructDeclarationNode>() &&
				node.as<StructDeclarationNode>().name() ==
					StringTable::getOrInternStringHandle("MemberTemplateOwner")) {
				owner = &node.as<StructDeclarationNode>();
				break;
			}
		}
		REQUIRE(owner != nullptr);
		REQUIRE(owner->has_entity_id());

		const TemplateFunctionDeclarationNode* declaration = nullptr;
		const TemplateFunctionDeclarationNode* definition = nullptr;
		const TemplateFunctionDeclarationNode* address = nullptr;
		for (const StructMemberFunctionDecl& member : owner->member_functions()) {
			if (!member.function_declaration.is<TemplateFunctionDeclarationNode>()) {
				continue;
			}
			const TemplateFunctionDeclarationNode& candidate =
				member.function_declaration.as<TemplateFunctionDeclarationNode>();
			const StringHandle name =
				candidate.function_decl_node().decl_node().identifier_token().handle();
			if (StringTable::getStringView(name) == "select"sv) {
				if (candidate.function_decl_node().has_template_body_position()) {
					definition = &candidate;
				} else {
					declaration = &candidate;
				}
			} else if (StringTable::getStringView(name) == "address"sv) {
				address = &candidate;
			}
		}
		REQUIRE(declaration != nullptr);
		REQUIRE(definition != nullptr);
		REQUIRE(address != nullptr);
		REQUIRE(declaration->has_template_decl_id());
		REQUIRE(definition->has_template_decl_id());
		REQUIRE(address->has_template_decl_id());
		CHECK(declaration->template_decl_id() == definition->template_decl_id());
		CHECK(address->template_decl_id() != definition->template_decl_id());

		const FunctionDeclarationNode& selected = definition->function_decl_node();
		const TypeSpecifierNode& return_type = selected.decl_node().type_specifier_node();
		REQUIRE(return_type.has_template_parameter_decl());
		CHECK(return_type.template_decl_id() == definition->template_decl_id());
		CHECK(return_type.template_parameter_index() == 1u);
		REQUIRE(selected.parameter_nodes().size() == 2u);
		const TypeSpecifierNode& first_param =
			selected.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node();
		const TypeSpecifierNode& second_param =
			selected.parameter_nodes()[1].as<DeclarationNode>().type_specifier_node();
		REQUIRE(first_param.has_template_parameter_decl());
		REQUIRE(second_param.has_template_parameter_decl());
		CHECK(first_param.template_decl_id() == definition->template_decl_id());
		CHECK(first_param.template_parameter_index() == 0u);
		CHECK(second_param.template_decl_id() == definition->template_decl_id());
		CHECK(second_param.template_parameter_index() == 1u);

		CanonicalTypeTable& table = context.canonicalTypes();
		const CanonicalTypeImport return_import = importCanonicalType(table, return_type);
		REQUIRE(return_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(return_import.type == table.templateParameter(definition->template_decl_id(), 1u));
		const CanonicalTypeImport first_import = importCanonicalType(table, first_param);
		REQUIRE(first_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(first_import.type == table.templateParameter(definition->template_decl_id(), 0u));
	}

	TEST_CASE("Nested class publishes EntityId at parse time under class-owned OwnerId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct Inner { int other; };\n"
			"struct Outer { struct Inner { int tag; }; Inner value; };\n"
			"struct Second { struct Inner { short flag; }; Inner item; };\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_class_early_entity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StructDeclarationNode* global_inner = nullptr;
		const StructDeclarationNode* outer = nullptr;
		const StructDeclarationNode* second = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (!node.is<StructDeclarationNode>()) {
				continue;
			}
			const StructDeclarationNode& struct_decl = node.as<StructDeclarationNode>();
			const StringHandle name = struct_decl.name();
			if (name == StringTable::getOrInternStringHandle("Inner")) {
				global_inner = &struct_decl;
			} else if (name == StringTable::getOrInternStringHandle("Outer")) {
				outer = &struct_decl;
			} else if (name == StringTable::getOrInternStringHandle("Second")) {
				second = &struct_decl;
			}
		}
		REQUIRE(global_inner != nullptr);
		REQUIRE(outer != nullptr);
		REQUIRE(second != nullptr);
		REQUIRE(global_inner->has_entity_id());

		REQUIRE(outer->nested_classes().size() == 1u);
		REQUIRE(outer->nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& outer_inner =
			outer->nested_classes()[0].as<StructDeclarationNode>();
		REQUIRE(second->nested_classes().size() == 1u);
		REQUIRE(second->nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& second_inner =
			second->nested_classes()[0].as<StructDeclarationNode>();

		// Parse-time identity: nested classes publish during their own body
		// parse, and same-spelling nested classes never share the namespace-level
		// entity that the global Inner definition owns.
		REQUIRE(outer_inner.has_entity_id());
		REQUIRE(second_inner.has_entity_id());
		const EntityId outer_inner_entity = outer_inner.entity_id();
		const EntityId second_inner_entity = second_inner.entity_id();
		CHECK(outer_inner_entity != global_inner->entity_id());
		CHECK(second_inner_entity != global_inner->entity_id());
		CHECK(outer_inner_entity != second_inner_entity);

		DeclarationBuilder& builder = context.declarationBuilder();
		const EntityRecord& outer_inner_record = builder.entity(outer_inner_entity);
		const EntityRecord& second_inner_record = builder.entity(second_inner_entity);
		const EntityRecord& global_record = builder.entity(global_inner->entity_id());
		CHECK(isClassOwnedOwnerId(outer_inner_record.owner_id));
		CHECK(isClassOwnedOwnerId(second_inner_record.owner_id));
		CHECK_FALSE(isClassOwnedOwnerId(global_record.owner_id));
		CHECK(outer_inner_record.owner_id ==
			  ownerIdFromClassEntity(outer->entity_id()));
		CHECK(second_inner_record.owner_id ==
			  ownerIdFromClassEntity(second->entity_id()));
		CHECK((outer_inner_record.flags & DeclarationFlags::IsDefinition) != 0u);
		CHECK((second_inner_record.flags & DeclarationFlags::IsDefinition) != 0u);
	}

	TEST_CASE("Nested forward declaration merges into the definition EntityId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct Outer { struct Inner; struct Inner { int tag; }; Inner value; };\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_class_forward_merge_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StructDeclarationNode* outer = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (node.is<StructDeclarationNode>() &&
				node.as<StructDeclarationNode>().name() ==
					StringTable::getOrInternStringHandle("Outer")) {
				outer = &node.as<StructDeclarationNode>();
				break;
			}
		}
		REQUIRE(outer != nullptr);
		REQUIRE(outer->nested_classes().size() == 2u);
		REQUIRE(outer->nested_classes()[0].is<StructDeclarationNode>());
		REQUIRE(outer->nested_classes()[1].is<StructDeclarationNode>());
		const StructDeclarationNode& forward =
			outer->nested_classes()[0].as<StructDeclarationNode>();
		const StructDeclarationNode& definition =
			outer->nested_classes()[1].as<StructDeclarationNode>();
		REQUIRE(forward.has_entity_id());
		REQUIRE(definition.has_entity_id());
		CHECK(forward.entity_id() == definition.entity_id());

		const EntityRecord& record =
			context.declarationBuilder().entity(definition.entity_id());
		CHECK(isClassOwnedOwnerId(record.owner_id));
		CHECK((record.flags & DeclarationFlags::IsDefinition) != 0u);
	}

	TEST_CASE("Member class template under published nested class publishes TemplateDeclId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct Outer {\n"
			"  struct Inner {\n"
			"    template<typename T> struct Box { T value; };\n"
			"    template<typename T, typename U> struct Pair { T left; U right; };\n"
			"  };\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_member_class_template_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StructDeclarationNode* outer = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (node.is<StructDeclarationNode>() &&
				node.as<StructDeclarationNode>().name() ==
					StringTable::getOrInternStringHandle("Outer")) {
				outer = &node.as<StructDeclarationNode>();
				break;
			}
		}
		REQUIRE(outer != nullptr);
		REQUIRE(outer->nested_classes().size() == 1u);
		REQUIRE(outer->nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& inner =
			outer->nested_classes()[0].as<StructDeclarationNode>();
		REQUIRE(inner.has_entity_id());

		const auto box_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Inner::Box"));
		REQUIRE(box_opt.has_value());
		REQUIRE(box_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& box = box_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(box.has_template_decl_id());
		REQUIRE(box.class_decl_node().has_template_decl_id());
		CHECK(box.template_decl_id() == box.class_decl_node().template_decl_id());

		const auto pair_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Inner::Pair"));
		REQUIRE(pair_opt.has_value());
		REQUIRE(pair_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& pair = pair_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(pair.has_template_decl_id());
		CHECK(pair.template_decl_id() != box.template_decl_id());

		// Both member primaries live under the nested class-owned OwnerId.
		TemplateDeclTable& template_decls = context.templateDecls();
		const OwnerId inner_owner = ownerIdFromClassEntity(inner.entity_id());
		CHECK(template_decls.findPrimaryClassTemplate(inner_owner,
			StringTable::getOrInternStringHandle("Box")).has_value());
		CHECK(template_decls.findPrimaryClassTemplate(inner_owner,
			StringTable::getOrInternStringHandle("Pair")).has_value());

		const auto& box_members = box.class_decl_node().members();
		REQUIRE(box_members.size() == 1u);
		REQUIRE(box_members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& value_type =
			box_members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(value_type.has_template_parameter_decl());
		CHECK(value_type.template_decl_id() == box.template_decl_id());
		CHECK(value_type.template_parameter_index() == 0u);

		CanonicalTypeTable& table = context.canonicalTypes();
		const CanonicalTypeImport value_import = importCanonicalType(table, value_type);
		REQUIRE(value_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(value_import.type == table.templateParameter(box.template_decl_id(), 0u));
	}

	TEST_CASE("Member function template under published nested class publishes TemplateDeclId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct Outer {\n"
			"  struct Inner {\n"
			"    template<typename T, typename U> U select(T left, U right);\n"
			"    template<typename T, typename U> U select(T left, U right) { return right; }\n"
			"    template<typename T> T* address(T* value) { return value; }\n"
			"  };\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("nested_member_function_template_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StructDeclarationNode* outer = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (node.is<StructDeclarationNode>() &&
				node.as<StructDeclarationNode>().name() ==
					StringTable::getOrInternStringHandle("Outer")) {
				outer = &node.as<StructDeclarationNode>();
				break;
			}
		}
		REQUIRE(outer != nullptr);
		REQUIRE(outer->nested_classes().size() == 1u);
		REQUIRE(outer->nested_classes()[0].is<StructDeclarationNode>());
		const StructDeclarationNode& inner =
			outer->nested_classes()[0].as<StructDeclarationNode>();
		REQUIRE(inner.has_entity_id());

		const TemplateFunctionDeclarationNode* declaration = nullptr;
		const TemplateFunctionDeclarationNode* definition = nullptr;
		const TemplateFunctionDeclarationNode* address = nullptr;
		for (const StructMemberFunctionDecl& member : inner.member_functions()) {
			if (!member.function_declaration.is<TemplateFunctionDeclarationNode>()) {
				continue;
			}
			const TemplateFunctionDeclarationNode& candidate =
				member.function_declaration.as<TemplateFunctionDeclarationNode>();
			const StringHandle name =
				candidate.function_decl_node().decl_node().identifier_token().handle();
			if (StringTable::getStringView(name) == "select"sv) {
				if (candidate.function_decl_node().has_template_body_position()) {
					definition = &candidate;
				} else {
					declaration = &candidate;
				}
			} else if (StringTable::getStringView(name) == "address"sv) {
				address = &candidate;
			}
		}
		REQUIRE(declaration != nullptr);
		REQUIRE(definition != nullptr);
		REQUIRE(address != nullptr);
		REQUIRE(declaration->has_template_decl_id());
		REQUIRE(definition->has_template_decl_id());
		REQUIRE(address->has_template_decl_id());
		CHECK(declaration->template_decl_id() == definition->template_decl_id());
		CHECK(address->template_decl_id() != definition->template_decl_id());

		const FunctionDeclarationNode& selected = definition->function_decl_node();
		const TypeSpecifierNode& return_type = selected.decl_node().type_specifier_node();
		REQUIRE(return_type.has_template_parameter_decl());
		CHECK(return_type.template_decl_id() == definition->template_decl_id());
		CHECK(return_type.template_parameter_index() == 1u);
		REQUIRE(selected.parameter_nodes().size() == 2u);
		const TypeSpecifierNode& first_param =
			selected.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node();
		const TypeSpecifierNode& second_param =
			selected.parameter_nodes()[1].as<DeclarationNode>().type_specifier_node();
		REQUIRE(first_param.has_template_parameter_decl());
		REQUIRE(second_param.has_template_parameter_decl());
		CHECK(first_param.template_decl_id() == definition->template_decl_id());
		CHECK(first_param.template_parameter_index() == 0u);
		CHECK(second_param.template_decl_id() == definition->template_decl_id());
		CHECK(second_param.template_parameter_index() == 1u);

		CanonicalTypeTable& table = context.canonicalTypes();
		const CanonicalTypeImport return_import = importCanonicalType(table, return_type);
		REQUIRE(return_import.status == CanonicalTypeImportStatus::Supported);
		CHECK(return_import.type == table.templateParameter(definition->template_decl_id(), 1u));
	}

	TEST_CASE("Member class template resolves qualified spellings by identity without registry chain keys") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"namespace ns {\n"
			"struct Outer {\n"
			"  struct Inner {\n"
			"    template<typename T> struct Box { T value; };\n"
			"  };\n"
			"};\n"
			"}\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_class_template_owner_chain_keys_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		// The legacy owner-prefix key and simple member name still answer.
		const auto legacy_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Inner::Box"));
		REQUIRE(legacy_opt.has_value());
		REQUIRE(legacy_opt->is<TemplateClassDeclarationNode>());

		// Owner-chain registry alias keys are deleted: qualified spellings
		// must not resolve through spelling aliases.
		CHECK_FALSE(gTemplateRegistry
			.lookupTemplate(StringTable::getOrInternStringHandle("Outer::Inner::Box"))
			.has_value());
		CHECK_FALSE(gTemplateRegistry
			.lookupTemplate(StringTable::getOrInternStringHandle("ns::Outer::Inner::Box"))
			.has_value());

		// The qualified spelling resolves through published identity to the
		// same declaration the legacy key answers.
		const std::optional<ASTNode> chain_pattern =
			parser.findClassTemplatePatternBySpelling("ns::Outer::Inner::Box");
		REQUIRE(chain_pattern.has_value());
		REQUIRE(chain_pattern->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& chain =
			chain_pattern->as<TemplateClassDeclarationNode>();
		CHECK(chain.class_decl_node().has_template_decl_id());
		CHECK(legacy_opt->as<TemplateClassDeclarationNode>().class_decl_node().template_decl_id() ==
			  chain.class_decl_node().template_decl_id());

		// Fail-closed: an unresolvable chain finds no pattern and no alias.
		CHECK_FALSE(parser.findClassTemplatePatternBySpelling("ns::Missing::Box").has_value());
	}

	TEST_CASE("Member alias template resolves qualified spellings through published identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"namespace ns {\n"
			"struct Gauge {\n"
			"  template<typename T> using Meter = T;\n"
			"};\n"
			"}\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_alias_template_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		// The legacy owner-prefix key still answers at parse time.
	const auto legacy_opt =
		gTemplateRegistry.lookup_alias_template("ns::Gauge::Meter");
	REQUIRE(legacy_opt.has_value());
	REQUIRE(legacy_opt->is<TemplateAliasNode>());
	const TemplateAliasNode& member_alias = legacy_opt->as<TemplateAliasNode>();
	REQUIRE(member_alias.has_template_decl_id());
	const std::optional<TypeId> member_alias_target =
		context.canonicalTypes().aliasTemplateTarget(member_alias.template_decl_id());
	REQUIRE(member_alias_target.has_value());
	CHECK(*member_alias_target == context.canonicalTypes().templateParameter(
		member_alias.template_decl_id(), 0));

	// The partial namespace suffix spelling resolves through published
		// identity to the same alias node the legacy key answers.
		const std::optional<ASTNode> chain_alias =
			parser.findAliasTemplateByIdentityChain("ns::Gauge::Meter");
		REQUIRE(chain_alias.has_value());
		REQUIRE(chain_alias->is<TemplateAliasNode>());
		CHECK(&chain_alias->as<TemplateAliasNode>() == &legacy_opt->as<TemplateAliasNode>());
		const std::optional<ASTNode> suffix_alias =
			parser.findAliasTemplateBySpelling("Gauge::Meter");
		REQUIRE(suffix_alias.has_value());
		REQUIRE(suffix_alias->is<TemplateAliasNode>());
		CHECK(&suffix_alias->as<TemplateAliasNode>() == &legacy_opt->as<TemplateAliasNode>());

		// Fail-closed: an unresolvable owner chain and a missing member find
		// no alias.
		CHECK_FALSE(parser.findAliasTemplateByIdentityChain("Missing::Meter").has_value());
		CHECK_FALSE(parser.findAliasTemplateByIdentityChain("ns::Gauge::NoMember").has_value());
	}

	TEST_CASE("Member alias target captures enclosing template parameters") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename Owner> struct Captures {\n"
			"  template<typename Value> using Pointer = Owner*;\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_alias_captured_owner_target_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto member_alias = gTemplateRegistry.lookup_alias_template("Captures::Pointer");
		REQUIRE(member_alias.has_value());
		REQUIRE(member_alias->is<TemplateAliasNode>());
		REQUIRE(member_alias->as<TemplateAliasNode>().has_template_decl_id());
		CHECK(member_alias->as<TemplateAliasNode>().target_type_node().has_template_parameter_decl());
		const TemplateDeclId alias_decl = member_alias->as<TemplateAliasNode>().template_decl_id();
		const auto target = context.canonicalTypes().aliasTemplateTarget(alias_decl);
		REQUIRE(target.has_value());
		const TemplateDeclId owner_decl = context.canonicalTypes().templateParameterDecl(
			context.canonicalTypes().node(*target).child);
		const TypeId owner_arg[] = {context.canonicalTypes().builtin(CanonicalBuiltinKind::Int)};
		const TypeId alias_arg[] = {context.canonicalTypes().builtin(CanonicalBuiltinKind::Char)};
		const TypeId owner_spec = context.canonicalTypes().templateSpecialization(owner_decl, owner_arg);
		CHECK(context.canonicalTypes().resolveMemberAliasTarget(alias_decl, owner_spec, alias_arg) ==
			context.canonicalTypes().pointer(owner_arg[0]));
	}

	TEST_CASE("Qualified member alias use stamps declaration identity and auto-redirects") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		// A concrete `Captures<int>::Pointer<char>` is fully resolved by the
		// legacy path before canonical import, so no canonical use survives. The
		// dependent owner `Captures<T>` keeps the qualified member-alias use
		// canonical, which is the form the auto-redirect is wired for.
		const std::string code =
			"template<typename Owner> struct Captures {\n"
			"  template<typename Value> using Pointer = Owner*;\n"
			"};\n"
			"template<typename T> struct Use {\n"
			"  typename Captures<T>::template Pointer<char> value;\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_alias_use_auto_redirect_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto alias_opt = gTemplateRegistry.lookup_alias_template("Captures::Pointer");
		REQUIRE(alias_opt.has_value());
		REQUIRE(alias_opt->is<TemplateAliasNode>());
		REQUIRE(alias_opt->as<TemplateAliasNode>().has_template_decl_id());
		const TemplateDeclId alias_decl = alias_opt->as<TemplateAliasNode>().template_decl_id();

		const auto use_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Use"));
		REQUIRE(use_opt.has_value());
		REQUIRE(use_opt->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& use = use_opt->as<TemplateClassDeclarationNode>();
		REQUIRE(use.has_template_decl_id());
		const auto& members = use.class_decl_node().members();
		REQUIRE(members.size() == 1u);
		REQUIRE(members[0].declaration.is<DeclarationNode>());
		const TypeSpecifierNode& use_type =
			members[0].declaration.as<DeclarationNode>().type_specifier_node();
		REQUIRE(use_type.has_dependent_name_type());

		// The qualified use carries the published alias declaration identity,
		// not a spelling name.
		CanonicalTypeTable& table = context.canonicalTypes();
		const TypeId stamp = use_type.dependent_name_type();
		REQUIRE(table.node(stamp).kind == CanonicalTypeKind::DependentMemberAlias);
		CHECK(table.dependentMemberAliasDecl(stamp) == alias_decl);
		CHECK(table.node(table.node(stamp).child).kind ==
			  CanonicalTypeKind::TemplateSpecialization);

		// Substituting the owner argument auto-redirects through the carried
		// identity to the resolved pointer target.
		const TypeId owner_arg[] = {table.builtin(CanonicalBuiltinKind::Int)};
		const TypeId substituted =
			table.substitute(stamp, use.template_decl_id(), owner_arg);
		const TypeId resolved = table.tryResolveDependentTip(substituted);
		CHECK(resolved == table.pointer(owner_arg[0]));
	}

	TEST_CASE("Nested owner and alias argument member alias target publishes") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<class First, class Second> struct Both { First first; Second second; };\n"
			"template<class Owner> struct Captures {\n"
			"  template<class Value> using Pointer = Owner*;\n"
			"  template<class Value> using Mixed = Both<Owner, Value>;\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_alias_nested_owner_argument_publication_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		// The plain owner-capturing pointer target stays directly representable.
		const auto pointer_alias = gTemplateRegistry.lookup_alias_template("Captures::Pointer");
		REQUIRE(pointer_alias.has_value());
		REQUIRE(pointer_alias->is<TemplateAliasNode>());
		REQUIRE(pointer_alias->as<TemplateAliasNode>().has_template_decl_id());
		const TemplateDeclId pointer_decl =
			pointer_alias->as<TemplateAliasNode>().template_decl_id();
		const auto pointer_target = context.canonicalTypes().aliasTemplateTarget(pointer_decl);
		REQUIRE(pointer_target.has_value());

		// The nested Both<Owner, Value> target now publishes. Both arguments are
		// stamped after the alias TemplateDeclId exists: Owner to the enclosing
		// owner declaration and Value to the member alias declaration.
		const auto mixed_alias = gTemplateRegistry.lookup_alias_template("Captures::Mixed");
		REQUIRE(mixed_alias.has_value());
		REQUIRE(mixed_alias->is<TemplateAliasNode>());
		REQUIRE(mixed_alias->as<TemplateAliasNode>().has_template_decl_id());
		const TemplateDeclId mixed_decl = mixed_alias->as<TemplateAliasNode>().template_decl_id();
		const auto mixed_target = context.canonicalTypes().aliasTemplateTarget(mixed_decl);
		REQUIRE(mixed_target.has_value());

		const auto both_opt =
			gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle("Both"));
		REQUIRE(both_opt.has_value());
		REQUIRE(both_opt->is<TemplateClassDeclarationNode>());
		const TemplateDeclId both_decl =
			both_opt->as<TemplateClassDeclarationNode>().template_decl_id();
		REQUIRE(both_decl);

		// Resolving the published target with concrete owner and alias arguments
		// redirects to Both<owner, alias> with owner arguments first.
		const TemplateDeclId owner_decl = context.canonicalTypes().templateParameterDecl(
			context.canonicalTypes().node(*pointer_target).child);
		const TypeId owner_arg[] = {context.canonicalTypes().builtin(CanonicalBuiltinKind::Int)};
		const TypeId alias_arg[] = {context.canonicalTypes().builtin(CanonicalBuiltinKind::Char)};
		const TypeId owner_spec =
			context.canonicalTypes().templateSpecialization(owner_decl, owner_arg);
		const auto resolved = context.canonicalTypes()
			.resolveMemberAliasTarget(mixed_decl, owner_spec, alias_arg);
		REQUIRE(resolved.has_value());
		const TypeId expected = context.canonicalTypes().templateSpecialization(
			both_decl, std::array<TypeId, 2>{owner_arg[0], alias_arg[0]});
		CHECK(*resolved == expected);
	}


	TEST_CASE("Namespace and global alias templates publish declaration identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"namespace ns {\n"
			"template<typename T> using Meter = T;\n"
			"}\n"
			"template<typename T> using GlobalMeter = T;\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("namespace_alias_template_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		// The legacy registry spelling keys still answer.
		const auto legacy_ns = gTemplateRegistry.lookup_alias_template("ns::Meter");
		REQUIRE(legacy_ns.has_value());
		REQUIRE(legacy_ns->is<TemplateAliasNode>());
		const auto legacy_global = gTemplateRegistry.lookup_alias_template("GlobalMeter");
		REQUIRE(legacy_global.has_value());
		REQUIRE(legacy_global->is<TemplateAliasNode>());

		// Identity is keyed by namespace OwnerId + name and anchors the same
		// alias node the registry spelling answers.
		const OwnerId global_owner =
			ownerIdFromNamespaceHandle(NamespaceRegistry::GLOBAL_NAMESPACE);
		const std::optional<TemplateDeclId> global_primary =
			context.templateDecls().findPrimaryAliasTemplate(
				global_owner, StringTable::getOrInternStringHandle("GlobalMeter"));
		REQUIRE(global_primary.has_value());
		const std::optional<ASTNode> global_pattern =
			context.templateDecls().primaryAliasPattern(*global_primary);
		REQUIRE(global_pattern.has_value());
		REQUIRE(global_pattern->is<TemplateAliasNode>());
		CHECK(&global_pattern->as<TemplateAliasNode>() ==
			  &legacy_global->as<TemplateAliasNode>());

		const NamespaceHandle ns_handle =
			gSymbolTable.resolve_namespace_handle("ns", false);
		REQUIRE(ns_handle.isValid());
		const OwnerId ns_owner = ownerIdFromNamespaceHandle(ns_handle);
		const std::optional<TemplateDeclId> ns_primary =
			context.templateDecls().findPrimaryAliasTemplate(
				ns_owner, StringTable::getOrInternStringHandle("Meter"));
		REQUIRE(ns_primary.has_value());
		const std::optional<ASTNode> ns_pattern =
			context.templateDecls().primaryAliasPattern(*ns_primary);
		REQUIRE(ns_pattern.has_value());
		REQUIRE(ns_pattern->is<TemplateAliasNode>());
		CHECK(&ns_pattern->as<TemplateAliasNode>() ==
			  &legacy_ns->as<TemplateAliasNode>());
		CHECK(ns_owner != global_owner);

		// Fail-closed: an unpublished name finds no identity.
		CHECK_FALSE(context.templateDecls()
			.findPrimaryAliasTemplate(
				global_owner, StringTable::getOrInternStringHandle("Missing"))
			.has_value());
	}

	TEST_CASE("Instantiated-owner member alias resolves through published identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename T> struct WideOwner {\n"
			"  template<typename U> using Meter = char;\n"
			"};\n"
			"template<typename T> struct NarrowOwner {\n"
			"  template<typename U> using Meter = U;\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("instantiated_owner_member_alias_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		TemplateArgumentVector long_args;
		long_args.push_back(TemplateTypeArg::makeType(
			nativeTypeIndex(TypeCategory::LongLong)));
		TemplateArgumentVector char_args;
		char_args.push_back(TemplateTypeArg::makeType(
			nativeTypeIndex(TypeCategory::Char)));

		const std::optional<ASTNode> wide_instance =
			parser.instantiateClassTemplateForSignatureReplay(
				"WideOwner", long_args, false);
		REQUIRE(wide_instance.has_value());
		REQUIRE(wide_instance->is<StructDeclarationNode>());
		const std::optional<ASTNode> narrow_instance =
			parser.instantiateClassTemplateForSignatureReplay(
				"NarrowOwner", char_args, false);
		REQUIRE(narrow_instance.has_value());
		REQUIRE(narrow_instance->is<StructDeclarationNode>());

		const std::string_view wide_inst_name =
			StringTable::getStringView(wide_instance->as<StructDeclarationNode>().name());
		const std::string_view narrow_inst_name =
			StringTable::getStringView(narrow_instance->as<StructDeclarationNode>().name());
		REQUIRE(wide_inst_name != narrow_inst_name);

		const std::string wide_meter =
			std::string(wide_inst_name) + "::Meter";
		const std::string narrow_meter =
			std::string(narrow_inst_name) + "::Meter";

		// Instantiated-owner spellings resolve through the injected primary
		// pattern to each owner's published alias TemplateDeclId.
		const std::optional<ASTNode> wide_alias =
			parser.findAliasTemplateByIdentityChain(wide_meter);
		REQUIRE(wide_alias.has_value());
		REQUIRE(wide_alias->is<TemplateAliasNode>());
		const std::optional<ASTNode> narrow_alias =
			parser.findAliasTemplateByIdentityChain(narrow_meter);
		REQUIRE(narrow_alias.has_value());
		REQUIRE(narrow_alias->is<TemplateAliasNode>());
		CHECK(&wide_alias->as<TemplateAliasNode>() !=
			  &narrow_alias->as<TemplateAliasNode>());

		// Spelling helper answers the same nodes; pattern spellings remain the
		// legacy registration keys.
		const auto wide_legacy =
			gTemplateRegistry.lookup_alias_template("WideOwner::Meter");
		const auto narrow_legacy =
			gTemplateRegistry.lookup_alias_template("NarrowOwner::Meter");
		REQUIRE(wide_legacy.has_value());
		REQUIRE(narrow_legacy.has_value());
		CHECK(&wide_alias->as<TemplateAliasNode>() ==
			  &wide_legacy->as<TemplateAliasNode>());
		CHECK(&narrow_alias->as<TemplateAliasNode>() ==
			  &narrow_legacy->as<TemplateAliasNode>());
		CHECK(parser.findAliasTemplateBySpelling(wide_meter).has_value());
		CHECK(parser.findAliasTemplateBySpelling(narrow_meter).has_value());
	}


	TEST_CASE("Instantiated-owner member variable resolves through published identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"template<typename T> struct WideOwner {\n"
			"  template<typename U> static constexpr int Meter = 1;\n"
			"};\n"
			"template<typename T> struct NarrowOwner {\n"
			"  template<typename U> static constexpr int Meter = 2;\n"
			"};\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("instantiated_owner_member_variable_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		TemplateArgumentVector long_args;
		long_args.push_back(TemplateTypeArg::makeType(
			nativeTypeIndex(TypeCategory::LongLong)));
		TemplateArgumentVector char_args;
		char_args.push_back(TemplateTypeArg::makeType(
			nativeTypeIndex(TypeCategory::Char)));

		const std::optional<ASTNode> wide_instance =
			parser.instantiateClassTemplateForSignatureReplay(
				"WideOwner", long_args, false);
		REQUIRE(wide_instance.has_value());
		REQUIRE(wide_instance->is<StructDeclarationNode>());
		const std::optional<ASTNode> narrow_instance =
			parser.instantiateClassTemplateForSignatureReplay(
				"NarrowOwner", char_args, false);
		REQUIRE(narrow_instance.has_value());
		REQUIRE(narrow_instance->is<StructDeclarationNode>());

		const std::string_view wide_inst_name =
			StringTable::getStringView(wide_instance->as<StructDeclarationNode>().name());
		const std::string_view narrow_inst_name =
			StringTable::getStringView(narrow_instance->as<StructDeclarationNode>().name());
		REQUIRE(wide_inst_name != narrow_inst_name);

		const std::string wide_meter = std::string(wide_inst_name) + "::Meter";
		const std::string narrow_meter = std::string(narrow_inst_name) + "::Meter";

		const std::optional<ASTNode> wide_variable =
			parser.findVariableTemplateByIdentityChain(wide_meter);
		REQUIRE(wide_variable.has_value());
		REQUIRE(wide_variable->is<TemplateVariableDeclarationNode>());
		const std::optional<ASTNode> narrow_variable =
			parser.findVariableTemplateByIdentityChain(narrow_meter);
		REQUIRE(narrow_variable.has_value());
		REQUIRE(narrow_variable->is<TemplateVariableDeclarationNode>());
		CHECK(&wide_variable->as<TemplateVariableDeclarationNode>() !=
			  &narrow_variable->as<TemplateVariableDeclarationNode>());

		const auto wide_legacy =
			gTemplateRegistry.lookupVariableTemplate("WideOwner::Meter");
		const auto narrow_legacy =
			gTemplateRegistry.lookupVariableTemplate("NarrowOwner::Meter");
		REQUIRE(wide_legacy.has_value());
		REQUIRE(narrow_legacy.has_value());
		CHECK(&wide_variable->as<TemplateVariableDeclarationNode>() ==
			  &wide_legacy->as<TemplateVariableDeclarationNode>());
		CHECK(&narrow_variable->as<TemplateVariableDeclarationNode>() ==
			  &narrow_legacy->as<TemplateVariableDeclarationNode>());
		CHECK(parser.findVariableTemplateBySpelling(wide_meter).has_value());
		CHECK(parser.findVariableTemplateBySpelling(narrow_meter).has_value());
	}

	TEST_CASE("Member variable template resolves qualified spellings through published identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"namespace ns {\n"
			"struct Gauge {\n"
			"  template<typename T> static constexpr int Meter = 42;\n"
			"};\n"
			"}\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("member_variable_template_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		// The legacy enclosing-name key still answers at parse time.
		const auto legacy_opt =
			gTemplateRegistry.lookupVariableTemplate("Gauge::Meter");
		REQUIRE(legacy_opt.has_value());
		REQUIRE(legacy_opt->is<TemplateVariableDeclarationNode>());

		// The qualified spellings resolve through published identity to the
		// same variable node the legacy key answers.
		const std::optional<ASTNode> chain_variable =
			parser.findVariableTemplateByIdentityChain("ns::Gauge::Meter");
		REQUIRE(chain_variable.has_value());
		REQUIRE(chain_variable->is<TemplateVariableDeclarationNode>());
		CHECK(&chain_variable->as<TemplateVariableDeclarationNode>() ==
			  &legacy_opt->as<TemplateVariableDeclarationNode>());
		const std::optional<ASTNode> suffix_variable =
			parser.findVariableTemplateBySpelling("Gauge::Meter");
		REQUIRE(suffix_variable.has_value());
		REQUIRE(suffix_variable->is<TemplateVariableDeclarationNode>());
		CHECK(&suffix_variable->as<TemplateVariableDeclarationNode>() ==
			  &legacy_opt->as<TemplateVariableDeclarationNode>());

		// Fail-closed: an unresolvable owner chain and a missing member find
		// no variable template.
		CHECK_FALSE(parser.findVariableTemplateByIdentityChain("Missing::Meter").has_value());
		CHECK_FALSE(parser.findVariableTemplateByIdentityChain("ns::Gauge::NoMember").has_value());
	}

	TEST_CASE("NamespaceRegistry partial qualified-name suffixes follow parent chain") {
		const StringHandle name_a =
			StringTable::getOrInternStringHandle("partial_suffix_ns_a");
		const StringHandle name_b =
			StringTable::getOrInternStringHandle("partial_suffix_ns_b");
		const StringHandle name_c =
			StringTable::getOrInternStringHandle("partial_suffix_ns_c");
		const NamespaceHandle ns_a = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, name_a);
		const NamespaceHandle ns_b = gNamespaceRegistry.getOrCreateNamespace(ns_a, name_b);
		const NamespaceHandle ns_c = gNamespaceRegistry.getOrCreateNamespace(ns_b, name_c);

		std::vector<StringHandle> global_suffixes;
		gNamespaceRegistry.forEachPartialQualifiedNameSuffix(
			NamespaceRegistry::GLOBAL_NAMESPACE, [&](StringHandle suffix) {
				global_suffixes.push_back(suffix);
			});
		REQUIRE(global_suffixes.empty());

		std::vector<StringHandle> top_level;
		gNamespaceRegistry.forEachPartialQualifiedNameSuffix(ns_a, [&](StringHandle suffix) {
			top_level.push_back(suffix);
		});
		REQUIRE(top_level.empty());

		std::vector<StringHandle> depth_two;
		gNamespaceRegistry.forEachPartialQualifiedNameSuffix(ns_b, [&](StringHandle suffix) {
			depth_two.push_back(suffix);
		});
		REQUIRE(depth_two.size() == 1u);
		CHECK(depth_two[0] == name_b);

		std::vector<StringHandle> depth_three;
		gNamespaceRegistry.forEachPartialQualifiedNameSuffix(ns_c, [&](StringHandle suffix) {
			depth_three.push_back(suffix);
		});
		REQUIRE(depth_three.size() == 2u);
		CHECK(depth_three[0] == name_c);
		CHECK(depth_three[1] ==
			  gNamespaceRegistry.buildQualifiedIdentifier({name_b, name_c}));
		CHECK(depth_three[1] != gNamespaceRegistry.getQualifiedNameHandle(ns_c));
	}

	TEST_CASE("Identity-based owner-chain resolution distinguishes colliding spellings") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code =
			"struct OuterA { struct Inner { template<typename T> struct Box { T first; }; }; };\n"
			"struct OuterB { struct Inner { template<typename T> struct Box { double second; }; }; };\n";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("owner_chain_identity_resolution_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		// Both chains share every legacy registry spelling; identity resolution
		// must still answer with each owner's own published pattern.
		const std::optional<ASTNode> pattern_a =
			parser.findClassTemplatePatternByIdentityChain("OuterA::Inner::Box");
		REQUIRE(pattern_a.has_value());
		REQUIRE(pattern_a->is<TemplateClassDeclarationNode>());
		const std::optional<ASTNode> pattern_b =
			parser.findClassTemplatePatternByIdentityChain("OuterB::Inner::Box");
		REQUIRE(pattern_b.has_value());
		REQUIRE(pattern_b->is<TemplateClassDeclarationNode>());
		CHECK(pattern_a->as<TemplateClassDeclarationNode>().class_decl_node().template_decl_id() !=
			  pattern_b->as<TemplateClassDeclarationNode>().class_decl_node().template_decl_id());

		// The anchored pattern carries its published id.
		CHECK(pattern_a->as<TemplateClassDeclarationNode>().has_template_decl_id());
		CHECK(pattern_b->as<TemplateClassDeclarationNode>().has_template_decl_id());

		// The legacy instance-name and cache bridge must incorporate the same
		// published IDs. Mutation of either ID to the other would make the second
		// call hit the first owner's cache entry instead of materializing its own
		// pattern.
		TemplateArgumentVector short_args;
		short_args.push_back(TemplateTypeArg::makeType(
			nativeTypeIndex(TypeCategory::Short)));
		const std::optional<ASTNode> instance_a =
			parser.instantiateClassTemplateForSignatureReplay(
				"OuterA::Inner::Box", short_args, false);
		REQUIRE(instance_a.has_value());
		REQUIRE(instance_a->is<StructDeclarationNode>());
		const std::optional<ASTNode> instance_b =
			parser.instantiateClassTemplateForSignatureReplay(
				"OuterB::Inner::Box", short_args, false);
		REQUIRE(instance_b.has_value());
		REQUIRE(instance_b->is<StructDeclarationNode>());
		CHECK(instance_a->as<StructDeclarationNode>().name() !=
			  instance_b->as<StructDeclarationNode>().name());

		// Fail-closed: an unresolvable owner chain yields no pattern.
		CHECK_FALSE(parser.findClassTemplatePatternByIdentityChain("Missing::Box").has_value());
	}

	TEST_CASE("Free function body replay stamps dependent member template chains") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
struct ReplayMemberSource {
	using value_type = short;

	template <typename U>
	struct Rebind {
		template <typename V>
		struct Again {
			using type = V*;
		};
	};

	value_type value;
};

template<typename T>
int replay_member_chain(T source) {
	typedef typename T::value_type ReplayValue;
	typedef typename T::template Rebind<int>::template Again<long>::type ReplayPointer;
	ReplayValue copied = source.value;
	ReplayPointer pointer = nullptr;
	return static_cast<int>(copied) + (pointer == nullptr ? 0 : 1);
}


int main() {
	return replay_member_chain(ReplayMemberSource{9}) - 9;
}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("canonical_function_member_chain_replay.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		TemplateEngine template_engine;
		parser.attachTemplateEngine(template_engine);
		REQUIRE(!parser.parse().is_error());

		const StringHandle function_name =
			StringTable::getOrInternStringHandle("replay_member_chain");
		const auto template_opt = gTemplateRegistry.lookupTemplate(function_name);
		REQUIRE(template_opt.has_value());
		REQUIRE(template_opt->is<TemplateFunctionDeclarationNode>());
		const TemplateFunctionDeclarationNode& template_function =
			template_opt->as<TemplateFunctionDeclarationNode>();
		REQUIRE(template_function.has_template_decl_id());
		const TemplateDeclId template_decl_id = template_function.template_decl_id();

		const FunctionDeclarationNode* replayed_function = nullptr;
		for (size_t index = 0; index < parser.get_nodes().size(); ++index) {
			const ASTNode& node = parser.get_nodes()[index];
			if (!parser.isInstantiatedNode(index) || !node.is<FunctionDeclarationNode>()) {
				continue;
			}
			const FunctionDeclarationNode& candidate = node.as<FunctionDeclarationNode>();
			if (candidate.decl_node().identifier_token().handle() == function_name) {
				replayed_function = &candidate;
				break;
			}
		}
		REQUIRE(replayed_function != nullptr);
		REQUIRE(replayed_function->get_definition().has_value());
		const BlockNode& body = replayed_function->get_definition()->as<BlockNode>();
		const TypedefDeclarationNode* replayed_pointer_alias = nullptr;
		for (const ASTNode& statement : body.get_statements()) {
			if (statement.is<TypedefDeclarationNode>() &&
				statement.as<TypedefDeclarationNode>().alias_name() == "ReplayPointer"sv) {
				replayed_pointer_alias = &statement.as<TypedefDeclarationNode>();
				break;
			}
		}
		REQUIRE(replayed_pointer_alias != nullptr);
		const TypeSpecifierNode& replayed_pointer_type =
			replayed_pointer_alias->type_specifier_node();
		REQUIRE(replayed_pointer_type.has_dependent_name_type());

		CanonicalTypeTable& table = context.canonicalTypes();
		const TypeId owner = table.templateParameter(template_decl_id, 0u);
		const TypeId int_arg = table.builtin(CanonicalBuiltinKind::Int);
		const TypeId long_arg = table.builtin(CanonicalBuiltinKind::Long);
		const std::array<TypeId, 1> rebind_args = {int_arg};
		const std::array<TypeId, 1> again_args = {long_arg};
		const TypeId rebind = table.dependentTemplateMember(
			owner,
			"Rebind",
			std::span<const TypeId>(rebind_args));
		const TypeId again = table.dependentTemplateMember(
			rebind,
			"Again",
			std::span<const TypeId>(again_args));
		CHECK(replayed_pointer_type.dependent_name_type() ==
			table.dependentName(again, "type"));
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed VariableDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "int scope_stamp_var;";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_var_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle var_name = StringTable::getOrInternStringHandle("scope_stamp_var");
		const std::vector<ASTNode> symbols =
			gSymbolTable.lookup_all(StringTable::getStringView(var_name));
		REQUIRE(symbols.size() == 1u);
		REQUIRE(symbols[0].is<VariableDeclarationNode>());
		CHECK(symbols[0]
				  .as<VariableDeclarationNode>()
				  .declaration()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed TemplateVariableDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "template<class T> constexpr T scope_stamp_tmpl_var = T{};";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_tmpl_var_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle var_name = StringTable::getOrInternStringHandle("scope_stamp_tmpl_var");
		const std::vector<ASTNode> symbols =
			gSymbolTable.lookup_all(StringTable::getStringView(var_name));
		REQUIRE(symbols.size() == 1u);
		REQUIRE(symbols[0].is<TemplateVariableDeclarationNode>());
		CHECK(symbols[0]
				  .as<TemplateVariableDeclarationNode>()
				  .variable_declaration()
				  .declaration()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on EnumDeclarationNode") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope = table.currentScopeId();
		const StringHandle enum_name = StringTable::getOrInternStringHandle("scope_stamp_enum");
		ASTNode node = ASTNode::emplace_node<EnumDeclarationNode>(enum_name, true);
		REQUIRE(table.insert(StringTable::getStringView(enum_name), node));
		const std::vector<ASTNode> symbols = table.lookup_all(StringTable::getStringView(enum_name));
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<EnumDeclarationNode>().lexical_scope_id() == block_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on StructDeclarationNode") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope = table.currentScopeId();
		const StringHandle struct_name = StringTable::getOrInternStringHandle("scope_stamp_struct");
		ASTNode node = ASTNode::emplace_node<StructDeclarationNode>(struct_name, false);
		REQUIRE(table.insert(StringTable::getStringView(struct_name), node));
		const std::vector<ASTNode> symbols = table.lookup_all(StringTable::getStringView(struct_name));
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<StructDeclarationNode>().lexical_scope_id() == block_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on TypedefDeclarationNode") {
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token alias_token(Token::Type::Identifier, std::string_view("scope_stamp_typedef"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<TypedefDeclarationNode>(int_type, alias_token);
		REQUIRE(table.insert(std::string_view("scope_stamp_typedef"), node));
		const std::vector<ASTNode> symbols = table.lookup_all("scope_stamp_typedef");
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<TypedefDeclarationNode>().lexical_scope_id() == global_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed EnumDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "enum class scope_stamp_parsed_enum { A };";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_enum_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle enum_name = StringTable::getOrInternStringHandle("scope_stamp_parsed_enum");
		const std::vector<ASTNode> symbols =
			gSymbolTable.lookup_all(StringTable::getStringView(enum_name));
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<EnumDeclarationNode>().lexical_scope_id().value != 0u);
	}

	TEST_CASE("Parser stamps lexical ScopeId on parsed StructDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "struct scope_stamp_parsed_struct { int x; };";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_struct_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle struct_name =
			StringTable::getOrInternStringHandle("scope_stamp_parsed_struct");
		const StructDeclarationNode* parsed_struct = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (!node.is<StructDeclarationNode>()) {
				continue;
			}
			const StructDeclarationNode& struct_decl = node.as<StructDeclarationNode>();
			if (struct_decl.name() == struct_name) {
				parsed_struct = &struct_decl;
				break;
			}
		}
		REQUIRE(parsed_struct != nullptr);
		CHECK(parsed_struct->lexical_scope_id().value != 0u);
	}

	TEST_CASE("Parser stamps lexical ScopeId on parsed TypedefDeclarationNode") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "typedef int scope_stamp_parsed_typedef;";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_typedef_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle typedef_name =
			StringTable::getOrInternStringHandle("scope_stamp_parsed_typedef");
		const TypedefDeclarationNode* parsed_typedef = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (!node.is<TypedefDeclarationNode>()) {
				continue;
			}
			const TypedefDeclarationNode& typedef_decl = node.as<TypedefDeclarationNode>();
			if (typedef_decl.alias_token().handle() == typedef_name) {
				parsed_typedef = &typedef_decl;
				break;
			}
		}
		REQUIRE(parsed_typedef != nullptr);
		CHECK(parsed_typedef->lexical_scope_id().value != 0u);
	}

	static void requireScopeRecordMatches(
		const FrontendContext& context,
		const SymbolTable& table,
		ScopeId id) {
		REQUIRE(table.findScopeById(id) != nullptr);
		const ScopeRecord& record = context.scopeRecord(id);
		const ScopeMetadataView metadata = readScopeMetadata(table, id);
		CHECK(record.id == id);
		CHECK(record.parent_id == metadata.parent_id);
		CHECK(record.depth == metadata.depth);
		CHECK(record.scope_type == metadata.scope_type);
		CHECK(record.namespace_handle == metadata.namespace_handle);
		CHECK(record.reserved == 0);
	}

	TEST_CASE("FrontendContext owns a global ScopeRecord at construction") {
		FrontendContext context;
		REQUIRE(context.scopeRecordCount() == 1u);
		REQUIRE(context.scopeCount() == 1u);
		REQUIRE(context.currentScopeId().value == 1u);
		const ScopeRecord& global = context.scopeRecord(ScopeId{1});
		CHECK(global.id.value == 1u);
		CHECK(!global.parent_id);
		CHECK(global.depth == 1u);
		CHECK(global.scope_type == ScopeType::Global);
		CHECK(global.namespace_handle.index == NamespaceHandle::INVALID_HANDLE);
		CHECK(global.reserved == 0);
		CHECK(context.scopeArenaUsedBytes() == sizeof(ScopeRecord));
		CHECK(context.scopeArenaReservedBytes() ==
			  static_cast<uint64_t>(kScopeArenaChunkSize) * sizeof(ScopeRecord));
		CHECK(context.findScopeRecord(ScopeId{2}) == nullptr);
	}

	TEST_CASE("SymbolTable enter/exit/clear publish matching ScopeRecords into the active FrontendContext") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		requireScopeRecordMatches(context, table, table.currentScopeId());

		table.enter_scope(ScopeType::Block);
		const ScopeId block_id = table.currentScopeId();
		REQUIRE(block_id.value == 2u);
		REQUIRE(context.scopeRecordCount() == table.scopeCount());
		REQUIRE(context.currentScopeId() == block_id);
		requireScopeRecordMatches(context, table, block_id);
		CHECK(context.scopeRecord(block_id).parent_id.value == 1u);
		CHECK(context.scopeRecord(block_id).scope_type == ScopeType::Block);

		const StringHandle ns_name = StringTable::getOrInternStringHandle("ScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_id = table.currentScopeId();
		REQUIRE(namespace_id.value == 3u);
		REQUIRE(context.scopeRecordCount() == 3u);
		REQUIRE(context.currentScopeId() == namespace_id);
		requireScopeRecordMatches(context, table, namespace_id);
		CHECK(context.scopeRecord(namespace_id).scope_type == ScopeType::Namespace);
		CHECK(context.scopeRecord(namespace_id).namespace_handle == ns_handle);

		table.exit_scope();
		CHECK(table.currentScopeId() == block_id);
		CHECK(context.currentScopeId() == block_id);
		CHECK(context.scopeRecordCount() == 3u);
		CHECK(table.scopeCount() == 3u);
		requireScopeRecordMatches(context, table, namespace_id);

		table.exit_scope();
		CHECK(table.currentScopeId().value == 1u);
		CHECK(context.currentScopeId().value == 1u);
		CHECK(context.scopeRecordCount() == 3u);

		table.enter_scope(ScopeType::Function);
		const ScopeId function_id = table.currentScopeId();
		REQUIRE(function_id.value == 4u);
		REQUIRE(context.scopeRecordCount() == 4u);
		requireScopeRecordMatches(context, table, function_id);
		CHECK(context.scopeRecord(function_id).scope_type == ScopeType::Function);

		table.clear();
		CHECK(table.scopeCount() == 1u);
		CHECK(context.scopeRecordCount() == 1u);
		CHECK(context.currentScopeId().value == 1u);
		requireScopeRecordMatches(context, table, ScopeId{1});
		CHECK(context.findScopeRecord(block_id) == nullptr);
	}

	TEST_CASE("lookup reads ScopeRecord metadata when persistent publication is enabled") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("LookupScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_scope_id = table.currentScopeId();

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("scope_record_lookup_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(table.insert(std::string_view("scope_record_lookup_probe"), node));

		const std::optional<ASTNode> found = table.lookup("scope_record_lookup_probe");
		REQUIRE(found.has_value());
		CHECK(found->is<DeclarationNode>());
		CHECK(table.get_current_namespace_handle() == ns_handle);

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Block,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});
		CHECK(table.legacyScopeMetadata(namespace_scope_id).scope_type == ScopeType::Block);

		const std::optional<ASTNode> found_after_poison = table.lookup("scope_record_lookup_probe");
		REQUIRE(found_after_poison.has_value());
		CHECK(found_after_poison->is<DeclarationNode>());
		CHECK(table.get_current_namespace_handle() == ns_handle);

		const std::vector<ASTNode> overloads = table.lookup_all("scope_record_lookup_probe");
		REQUIRE(overloads.size() == 1u);
		CHECK(overloads[0].is<DeclarationNode>());
	}

	TEST_CASE("insert enter and exit read ScopeRecord metadata when persistent publication is enabled") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("InsertScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_scope_id = table.currentScopeId();

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("insert_scope_record_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(table.insert(std::string_view("insert_scope_record_probe"), node));

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Block,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});

		Token id_token2(Token::Type::Identifier, std::string_view("insert_scope_record_probe2"), 1, 2, 0);
		ASTNode node2 = ASTNode::emplace_node<DeclarationNode>(int_type, id_token2);
		REQUIRE(table.insert(std::string_view("insert_scope_record_probe2"), node2));
		REQUIRE(table.lookup("insert_scope_record_probe2").has_value());

		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope_id = table.currentScopeId();
		table.mutateLegacyScopeMetadataForTest(
			block_scope_id,
			ScopeType::Function,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});
		table.exit_scope();
		CHECK(table.currentScopeId() == namespace_scope_id);
		CHECK(table.get_current_scope_type() == ScopeType::Namespace);

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Namespace,
			ScopeId{1},
			999u,
			ns_handle);
		table.enter_scope(ScopeType::Function);
		const ScopeId function_scope_id = table.currentScopeId();
		CHECK(context.scopeRecord(function_scope_id).depth ==
			  context.scopeRecord(namespace_scope_id).depth + 1u);
		CHECK(table.activeScopeDepth() == context.scopeRecord(function_scope_id).depth);
	}

	TEST_CASE("DeclarationBuilder publication reads ScopeRecord metadata when persistent publication is enabled") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("PublicationScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_scope_id = table.currentScopeId();

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Block,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});

		const StringHandle name = StringTable::getOrInternStringHandle("publication_scope_record_probe");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			namespace_scope_id,
			name,
			TelemetryTypeId{71},
			TelemetryTypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
		REQUIRE_FALSE(prepared.isRejected());

		const PublishResult published = builder.publishFunction(request, table);
		CHECK(published.status == PublishStatus::Created);
		REQUIRE(published.entity_id);
		CHECK(builder.entity(published.entity_id).owner_id == ownerIdFromNamespaceHandle(ns_handle));
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication rejects absent ScopeId without throwing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle name =
			StringTable::getOrInternStringHandle("decl_builder_absent_scope");
		const FunctionDeclRequest invalid = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TelemetryTypeId{71},
			TelemetryTypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.prepareFunctionPublication(invalid, table).isRejected());
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication rejects block scope without throwing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		table.enablePersistentScopePublication();
		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope_id = table.currentScopeId();
		const StringHandle name =
			StringTable::getOrInternStringHandle("decl_builder_block_scope_fn");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			block_scope_id,
			name,
			TelemetryTypeId{71},
			TelemetryTypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.prepareFunctionPublication(request, table).isRejected());
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication throws on stale ScopeId") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		table.enablePersistentScopePublication();
		const StringHandle name =
			StringTable::getOrInternStringHandle("decl_builder_stale_scope");
		const FunctionDeclRequest invalid = makeFunctionDeclRequest(
			ScopeId{999},
			name,
			TelemetryTypeId{71},
			TelemetryTypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK_THROWS_AS(builder.prepareFunctionPublication(invalid, table), InternalError);
	}

	TEST_CASE("SymbolTable enter_scope without an active FrontendContext still succeeds") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		CHECK(table.currentScopeId().value == 2u);
		CHECK(table.scopeCount() == 2u);
		table.exit_scope();
		CHECK(table.currentScopeId().value == 1u);
		CHECK(table.scopeCount() == 2u);
	}

	TEST_CASE("SymbolTable enablePersistentScopePublication requires an active FrontendContext") {
		SymbolTable table;
		bool threw = false;
		try {
			table.enablePersistentScopePublication();
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
		table.enter_scope(ScopeType::Block);
		CHECK(table.scopeCount() == 2u);
	}

	TEST_CASE("a second SymbolTable does not publish into the active FrontendContext") {
		FrontendContext context;
		SymbolTable publisher;
		publisher.enablePersistentScopePublication();
		publisher.enter_scope(ScopeType::Block);
		const std::size_t published = context.scopeRecordCount();
		REQUIRE(published == 2u);

		SymbolTable other;
		other.enter_scope(ScopeType::Function);
		other.enter_scope(ScopeType::Block);
		other.exit_scope();
		CHECK(context.scopeRecordCount() == published);
		CHECK(context.currentScopeId().value == 2u);
		CHECK(other.scopeCount() == 3u);
	}

	TEST_CASE("nested FrontendContext seeds its own ScopeRecord arena") {
		FrontendContext outer;
		SymbolTable outer_table;
		outer_table.enablePersistentScopePublication();
		outer_table.enter_scope(ScopeType::Block);
		REQUIRE(outer.scopeRecordCount() == 2u);
		{
			FrontendContext inner;
			CHECK(FrontendContext::active() == &inner);
			CHECK(inner.scopeRecordCount() == 1u);
			CHECK(outer.scopeRecordCount() == 2u);
			SymbolTable inner_table;
			inner_table.enablePersistentScopePublication();
			inner_table.enter_scope(ScopeType::Function);
			CHECK(inner.scopeRecordCount() == 2u);
			CHECK(inner.currentScopeId().value == 2u);
			CHECK(outer.scopeRecordCount() == 2u);
			CHECK(outer.currentScopeId().value == 2u);
		}
		CHECK(FrontendContext::active() == &outer);
		CHECK(outer.scopeRecordCount() == 2u);
		CHECK(outer.currentScopeId().value == 2u);
	}

	TEST_CASE("outer SymbolTable lookup uses its bound ScopeRecord arena under nested FrontendContext") {
		FrontendContext outer;
		SymbolTable outer_table;
		outer_table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("NestedLookupOuterNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		outer_table.enter_namespace(ns_handle);

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("nested_outer_lookup_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(outer_table.insert(std::string_view("nested_outer_lookup_probe"), node));
		REQUIRE(outer_table.lookup("nested_outer_lookup_probe").has_value());

		{
			FrontendContext inner;
			SymbolTable inner_table;
			inner_table.enablePersistentScopePublication();
			inner_table.enter_scope(ScopeType::Block);
			REQUIRE(FrontendContext::active() == &inner);
			REQUIRE(inner.scopeRecordCount() == 2u);
			REQUIRE(outer.scopeRecordCount() == 2u);

			const std::optional<ASTNode> found = outer_table.lookup("nested_outer_lookup_probe");
			REQUIRE(found.has_value());
			CHECK(found->is<DeclarationNode>());

			const std::vector<ASTNode> overloads = outer_table.lookup_all("nested_outer_lookup_probe");
			REQUIRE(overloads.size() == 1u);
			CHECK(overloads[0].is<DeclarationNode>());
		}
	}

	TEST_CASE("persistent ScopeId divergence from the arena is an InternalError") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		table.enter_scope(ScopeType::Block);
		context.resetPersistentScopes();
		REQUIRE(context.scopeRecordCount() == 1u);
		REQUIRE(table.scopeCount() == 2u);
		bool threw = false;
		try {
			table.enter_scope(ScopeType::Function);
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
	}

	TEST_CASE("SymbolTable enablePersistentScopePublication requires a global-only table and arena") {
		FrontendContext context;
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		bool threw_after_enter = false;
		try {
			table.enablePersistentScopePublication();
		} catch (const InternalError&) {
			threw_after_enter = true;
		}
		CHECK(threw_after_enter);

		SymbolTable publisher;
		publisher.enablePersistentScopePublication();
		publisher.enter_scope(ScopeType::Function);
		REQUIRE(context.scopeRecordCount() == 2u);
		SymbolTable too_late;
		bool threw_busy_context = false;
		try {
			too_late.enablePersistentScopePublication();
		} catch (const InternalError&) {
			threw_busy_context = true;
		}
		CHECK(threw_busy_context);
	}

	TEST_CASE("SymbolTable publication binding is cleared when bound FrontendContext is destroyed") {
		SymbolTable table;
		{
			FrontendContext context;
			table.enablePersistentScopePublication();
			table.enter_scope(ScopeType::Block);
			REQUIRE(table.persistentScopePublicationEnabled());
			REQUIRE(context.scopeRecordCount() == 2u);
		}
		CHECK_FALSE(table.persistentScopePublicationEnabled());
		CHECK(table.scopeCount() == 2u);
		CHECK(readScopeMetadata(table, ScopeId{2}).scope_type == ScopeType::Block);
	}

	TEST_CASE("bindPersistentScopePublication resyncs an already-published table") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		table.enter_scope(ScopeType::Block);
		table.enter_scope(ScopeType::Function);
		REQUIRE(context.scopeRecordCount() == 3u);
		REQUIRE(table.scopeCount() == 3u);

		bindPersistentScopePublication(table);

		CHECK(table.persistentScopePublicationEnabled());
		CHECK(table.currentScopeId().value == 1u);
		CHECK(table.scopeCount() == 1u);
		CHECK(context.scopeRecordCount() == 1u);
		CHECK(context.currentScopeId().value == 1u);
	}

	TEST_CASE("Parser parse publishes gSymbolTable scopes into the active FrontendContext") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		FrontendContext context;
		const std::string code = "int main() { return 0; }";
		CompileContext test_context;
		test_context.setInputFile("scope_record_parse_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(gSymbolTable.persistentScopePublicationEnabled());
		CHECK(context.scopeRecordCount() == gSymbolTable.scopeCount());
		CHECK(context.scopeRecordCount() > 1u);
		CHECK(context.currentScopeId() == gSymbolTable.currentScopeId());
		requireScopeRecordMatches(context, gSymbolTable, gSymbolTable.currentScopeId());
	}

	TEST_CASE("ChunkedVector reports used and reserved arena bytes") {
		ChunkedVector<DeclarationRecord, DeclarationBuilder::kDeclarationArenaChunkSize> arena;
		CHECK(arena.usedBytes() == 0);
		CHECK(arena.reservedBytes() == 0);

		DeclarationRecord record{};
		record.id = DeclId{1};
		arena.push_back(record);
		CHECK(arena.usedBytes() == sizeof(DeclarationRecord));
		CHECK(arena.reservedBytes() ==
			  static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord));
		CHECK(arena.reservedBytes() >= arena.usedBytes());

		arena.pop_back();
		CHECK(arena.usedBytes() == 0);
		CHECK(arena.reservedBytes() ==
			  static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord));
		CHECK(arena.peakUsedBytes() == sizeof(DeclarationRecord));
	}

	TEST_CASE("ChunkedAnyVector reports used and reserved arena bytes") {
		constexpr uint32_t kTestChunkSize = 256;
		ChunkedAnyVector<kTestChunkSize> storage;
		CHECK(storage.usedBytes() == 0);
		CHECK(storage.reservedBytes() == 0);

		TemplateEnvironmentSnapshotNode& node = storage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)node;
		CHECK(storage.usedBytes() >= sizeof(TemplateEnvironmentSnapshotNode));
		CHECK(storage.reservedBytes() >= kTestChunkSize);
		CHECK(storage.reservedBytes() >= storage.usedBytes());
	}

	TEST_CASE("FrontendContext semantic domain bytes track declaration arenas") {
		FrontendContext context;
		context.refreshSemanticDomainStats();
		CHECK(context.domainStats(AllocationDomain::Semantic).current_bytes == 0);
		CHECK(context.domainStats(AllocationDomain::Semantic).reserved_bytes == 0);
		CHECK(context.domainStats(AllocationDomain::Ir).current_bytes == 0);

		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("domain_bytes_first");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{10},
			TelemetryTypeId{20},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(request, table).status == PublishStatus::Created);

		context.refreshSemanticDomainStats();
		const DomainByteStats semantic = context.domainStats(AllocationDomain::Semantic);
		const uint64_t expected_used = sizeof(DeclarationRecord) + sizeof(EntityRecord);
		const uint64_t expected_reserved =
			static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord) +
			static_cast<uint64_t>(DeclarationBuilder::kEntityArenaChunkSize) * sizeof(EntityRecord);
		CHECK(semantic.current_bytes == expected_used);
		CHECK(semantic.peak_bytes == expected_used);
		CHECK(semantic.reserved_bytes == expected_reserved);
		CHECK(semantic.peak_reserved_bytes == expected_reserved);
		CHECK(builder.declarationArenaUsedBytes() == sizeof(DeclarationRecord));
		CHECK(builder.entityArenaUsedBytes() == sizeof(EntityRecord));
	}

	TEST_CASE("FrontendContext semantic domain peak survives publication rollback") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("domain_bytes_rollback");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{11},
			TelemetryTypeId{21},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		{
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
			transaction.rollback();
		}

		context.refreshSemanticDomainStats();
		const DomainByteStats semantic = context.domainStats(AllocationDomain::Semantic);
		CHECK(semantic.current_bytes == 0);
		CHECK(semantic.peak_bytes == sizeof(DeclarationRecord) + sizeof(EntityRecord));
		CHECK(semantic.reserved_bytes ==
			  static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord) +
			  static_cast<uint64_t>(DeclarationBuilder::kEntityArenaChunkSize) * sizeof(EntityRecord));
		CHECK(semantic.peak_reserved_bytes == semantic.reserved_bytes);
	}

	TEST_CASE("FrontendContext syntax domain bytes track the legacy AST bridge") {
		FrontendContext context;
		context.refreshSyntaxDomainStats();
		const uint64_t used_before = context.domainStats(AllocationDomain::Syntax).current_bytes;
		const uint64_t reserved_before = context.domainStats(AllocationDomain::Syntax).reserved_bytes;
		const std::size_t objects_before = gChunkedAnyStorage.size();

		TemplateEnvironmentSnapshotNode& node =
			gChunkedAnyStorage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)node;

		context.refreshSyntaxDomainStats();
		const DomainByteStats syntax = context.domainStats(AllocationDomain::Syntax);
		CHECK(gChunkedAnyStorage.size() == objects_before + 1);
		CHECK(syntax.current_bytes > used_before);
		CHECK(syntax.peak_bytes >= syntax.current_bytes);
		CHECK(syntax.reserved_bytes >= reserved_before);
		CHECK(syntax.reserved_bytes >= syntax.current_bytes);
		CHECK(context.domainStats(AllocationDomain::Ir).current_bytes == 0);
	}

	TEST_CASE("FrontendContext syntax AST family counts classify legacy bridge objects") {
		FrontendContext context;
		context.refreshSyntaxAstFamilyCounts();
		const std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> before =
			context.syntaxAstFamilyCounts();

		TemplateEnvironmentSnapshotNode& template_node =
			gChunkedAnyStorage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)template_node;
		BlockNode& block_node = gChunkedAnyStorage.emplace_back<BlockNode>();
		(void)block_node;

		context.refreshSyntaxAstFamilyCounts();
		const std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> after =
			context.syntaxAstFamilyCounts();
		CHECK(after[static_cast<std::size_t>(SyntaxAstFamily::Template)] ==
			  before[static_cast<std::size_t>(SyntaxAstFamily::Template)] + 1);
		CHECK(after[static_cast<std::size_t>(SyntaxAstFamily::Statement)] ==
			  before[static_cast<std::size_t>(SyntaxAstFamily::Statement)] + 1);
		CHECK(classifySyntaxAstFamily(std::type_index(typeid(TemplateEnvironmentSnapshotNode))) ==
			  SyntaxAstFamily::Template);
		CHECK(classifySyntaxAstFamily(std::type_index(typeid(BlockNode))) == SyntaxAstFamily::Statement);
	}

	TEST_CASE("FrontendContext semantic declaration kind counts track DeclarationBuilder records") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();

		context.refreshSemanticDeclKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> before_decls =
			context.declarationKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> before_entities =
			context.entityKindCounts();
		const std::size_t declarator_interns_before = builder.telemetryDeclaratorInternCount();
		const std::size_t parameter_lists_before = builder.telemetryParameterListInternCount();

		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			StringTable::getOrInternStringHandle("semantic_telemetry_fn"),
			TelemetryTypeId{901},
			TelemetryTypeId{902},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		{
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
			transaction.commit();
		}

		context.refreshSemanticDeclKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> after_decls =
			context.declarationKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> after_entities =
			context.entityKindCounts();
		CHECK(after_decls[static_cast<std::size_t>(DeclKind::Function)] ==
			  before_decls[static_cast<std::size_t>(DeclKind::Function)] + 1);
		CHECK(after_entities[static_cast<std::size_t>(DeclKind::Function)] ==
			  before_entities[static_cast<std::size_t>(DeclKind::Function)] + 1);
		CHECK(builder.telemetryDeclaratorInternCount() >= declarator_interns_before);
		CHECK(builder.telemetryParameterListInternCount() >= parameter_lists_before);
		CHECK(declKindLabel(DeclKind::Function) == "function");
	}

	TEST_CASE("FrontendContext IR domain stats record lowering buffer bytes") {
		FrontendContext context;
		context.recordIrDomainStats(128, 256);
		const DomainByteStats ir = context.domainStats(AllocationDomain::Ir);
		CHECK(ir.current_bytes == 128);
		CHECK(ir.peak_bytes == 128);
		CHECK(ir.reserved_bytes == 256);
		CHECK(ir.peak_reserved_bytes == 256);

		context.recordIrDomainStats(64, 512);
		const DomainByteStats ir_after_drop = context.domainStats(AllocationDomain::Ir);
		CHECK(ir_after_drop.current_bytes == 64);
		CHECK(ir_after_drop.peak_bytes == 128);
		CHECK(ir_after_drop.reserved_bytes == 512);
		CHECK(ir_after_drop.peak_reserved_bytes == 512);
	}

	TEST_CASE("Legacy ChunkedAnyVector emplace_back enforces allow-list on storage") {
		requireLegacyAstChunkedAnyEmplaceAllowed<TemplateEnvironmentSnapshotNode, true>();
		LegacyAstChunkedAnyVector storage;
		TemplateEnvironmentSnapshotNode& node = storage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)node;
		CHECK(isLegacyChunkedAnyStorageType<TemplateEnvironmentSnapshotNode>);
	}

	TEST_CASE("Legacy ChunkedAnyVector allow-list rejects new semantic record types") {
		struct ForbiddenNewSemanticRecord {
			uint32_t id;
		};
		static_assert(!isLegacyChunkedAnyStorageType<ForbiddenNewSemanticRecord>);
		static_assert(!isLegacyChunkedAnyStorageType<DeclarationRecord>);
		static_assert(!isLegacyChunkedAnyStorageType<EntityRecord>);
	}

	TEST_CASE("DeclarationBuilder creates DeclId and EntityId for first function") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_first");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{10},
			TelemetryTypeId{20},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult result = builder.publishFunction(request, table);
		CHECK(result.status == PublishStatus::Created);
		CHECK(result.decl_id.value == 1u);
		CHECK(result.entity_id.value == 1u);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
		CHECK(context.declarationCount() == 1u);
		CHECK(context.entityCount() == 1u);
		CHECK(sizeof(DeclarationRecord) == 32u);
		CHECK(sizeof(EntityRecord) == 32u);
		const DeclarationRecord& decl = builder.declaration(result.decl_id);
		CHECK(decl.entity_id == result.entity_id);
		CHECK_FALSE(decl.previous_decl_id);
		CHECK(decl.lexical_scope_id == global_scope);
		CHECK(decl.signature_id == TelemetryTypeId{10});
		CHECK(decl.return_type_id == TelemetryTypeId{20});
		CHECK(builder.entity(result.entity_id).owner_id == ownerIdFromNamespaceHandle(NamespaceRegistry::GLOBAL_NAMESPACE));
	}

	TEST_CASE("DeclarationBuilder creates DeclId and EntityId for first class") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_class_first");
		const ClassDeclRequest request {
			.lexical_scope_id = global_scope,
			.name = name,
			.is_definition = false,
		};
		const PublishResult result = builder.publishClass(request, table);
		CHECK(result.status == PublishStatus::Created);
		CHECK(result.decl_id.value == 1u);
		CHECK(result.entity_id.value == 1u);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
		const DeclarationRecord& decl = builder.declaration(result.decl_id);
		CHECK(decl.kind == static_cast<uint8_t>(DeclKind::Class));
		CHECK(decl.entity_id == result.entity_id);
		CHECK(decl.signature_id.value == 0u);
		CHECK(decl.return_type_id.value == 0u);
		CHECK(builder.entity(result.entity_id).kind == static_cast<uint8_t>(DeclKind::Class));
		CHECK(builder.entity(result.entity_id).first_decl_id == result.decl_id);
		CHECK(builder.entity(result.entity_id).latest_decl_id == result.decl_id);
	}

	TEST_CASE("DeclarationBuilder merges compatible class redeclaration into one EntityId") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_class_redecl");
		const ClassDeclRequest forward {
			.lexical_scope_id = global_scope,
			.name = name,
			.is_definition = false,
		};
		const ClassDeclRequest definition {
			.lexical_scope_id = global_scope,
			.name = name,
			.is_definition = true,
		};
		const PublishResult first = builder.publishClass(forward, table);
		const PublishResult second = builder.publishClass(definition, table);
		CHECK(first.status == PublishStatus::Created);
		CHECK(second.status == PublishStatus::MergedRedeclaration);
		CHECK(second.entity_id == first.entity_id);
		CHECK(second.decl_id != first.decl_id);
		CHECK(builder.declarationCount() == 2u);
		CHECK(builder.entityCount() == 1u);
		CHECK(builder.entity(first.entity_id).first_decl_id == first.decl_id);
		CHECK(builder.entity(first.entity_id).latest_decl_id == second.decl_id);
		CHECK(builder.declaration(second.decl_id).previous_decl_id == first.decl_id);
	}

	TEST_CASE("Forward-declared published nominal parameters import by EntityId") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();
		FrontendContext context;
		const std::string code =
			"struct IncompleteRecord;\n"
			"enum class IncompleteEnum : unsigned short;\n"
			"int inspect_incomplete_record_pointer(IncompleteRecord* value);\n"
			"int inspect_incomplete_record_reference(IncompleteRecord& value);\n"
			"int inspect_incomplete_enum_pointer(IncompleteEnum* value);\n"
			"int inspect_incomplete_enum_reference(IncompleteEnum& value);\n";
		CompileContext test_context;
		test_context.setInputFile("forward_declared_published_nominal_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const auto record_type_info = getTypesByNameMap().find(
			StringTable::getOrInternStringHandle("IncompleteRecord"));
		REQUIRE(record_type_info != getTypesByNameMap().end());
		REQUIRE(record_type_info->second->isStruct());
		const StructTypeInfo* struct_info = record_type_info->second->getStructInfo();
		REQUIRE(struct_info != nullptr);
		REQUIRE(struct_info->declaration_node != nullptr);
		const StructDeclarationNode& record_declaration = *struct_info->declaration_node;
		REQUIRE(record_declaration.is_forward_declaration());
		REQUIRE(record_declaration.has_entity_id());
		const EntityId record_entity = record_declaration.entity_id();

		const auto enum_type_info = getTypesByNameMap().find(
			StringTable::getOrInternStringHandle("IncompleteEnum"));
		REQUIRE(enum_type_info != getTypesByNameMap().end());
		const EnumTypeInfo* enum_info = enum_type_info->second->getEnumInfo();
		REQUIRE(enum_info != nullptr);
		REQUIRE(enum_info->declaration_node != nullptr);
		const EnumDeclarationNode& enum_declaration = *enum_info->declaration_node;
		REQUIRE(enum_declaration.is_forward_declaration());
		REQUIRE(enum_declaration.has_entity_id());
		const EntityId enum_entity = enum_declaration.entity_id();

		CanonicalTypeTable& table = context.canonicalTypes();
		CHECK_FALSE(table.hasRecordLayout(record_entity));
		CHECK_FALSE(table.hasEnumLayout(enum_entity));

		const uint64_t canonical_requests_before_import =
			context.declarationBuilder().canonicalDeclaratorRequests();
		CHECK(canonical_requests_before_import >= 4u);
		CHECK(context.declarationBuilder().unmigratedDeclaratorRequests() == 0u);
		const auto import_parameter = [&](std::string_view function_name,
			CanonicalTypeKind wrapper_kind, CanonicalTypeKind nominal_kind,
			EntityId expected_entity) {
			const std::vector<ASTNode> declarations = gSymbolTable.lookup_all(function_name);
			REQUIRE(declarations.size() == 1u);
			REQUIRE(declarations[0].is<FunctionDeclarationNode>());
			const std::span<const ASTNode> parameters =
				declarations[0].as<FunctionDeclarationNode>().parameter_nodes();
			REQUIRE(parameters.size() == 1u);
			REQUIRE(parameters[0].is<DeclarationNode>());
			TypeSpecifierNode parameter_type =
				parameters[0].as<DeclarationNode>().type_specifier_node();
			tryBindPublishedTypeEntity(parameter_type);
			REQUIRE(parameter_type.has_type_entity());
			CHECK(parameter_type.type_entity() == expected_entity);
			const size_t node_count_before_import = table.size();
			const CanonicalTypeImport imported =
				importCanonicalFunctionParameterType(table, parameter_type);
			REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
			CHECK(table.size() == node_count_before_import);
			REQUIRE(table.node(imported.type).kind == wrapper_kind);
			const TypeId nominal_type = table.node(imported.type).child;
			CHECK(table.node(nominal_type).kind == nominal_kind);
			if (nominal_kind == CanonicalTypeKind::Record) {
				CHECK(table.recordEntity(nominal_type) == expected_entity);
			} else {
				CHECK(table.enumEntity(nominal_type) == expected_entity);
			}
		};
		import_parameter(
			"inspect_incomplete_record_pointer",
			CanonicalTypeKind::Pointer,
			CanonicalTypeKind::Record,
			record_entity);
		import_parameter(
			"inspect_incomplete_record_reference",
			CanonicalTypeKind::LValueReference,
			CanonicalTypeKind::Record,
			record_entity);
		import_parameter(
			"inspect_incomplete_enum_pointer",
			CanonicalTypeKind::Pointer,
			CanonicalTypeKind::Enum,
			enum_entity);
		import_parameter(
			"inspect_incomplete_enum_reference",
			CanonicalTypeKind::LValueReference,
			CanonicalTypeKind::Enum,
			enum_entity);
	}

	TEST_CASE("Function-local record identity is owned by its lexical scope") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();
		FrontendContext context;
		const std::string code =
			"void first_local_record() {\n"
			"  struct LocalIdentity;\n"
			"  struct LocalIdentity* before;\n"
			"  struct LocalIdentity { int value; };\n"
			"  struct LocalIdentity* after;\n"
			"}\n"
			"void second_local_record() {\n"
			"  struct LocalIdentity { double value; };\n"
			"}\n";
		CompileContext test_context;
		test_context.setInputFile("function_local_record_identity_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StringHandle local_name =
			StringTable::getOrInternStringHandle("LocalIdentity");
		std::vector<const TypeInfo*> local_type_infos;
		for (const auto& [lookup_name, type_info] : getTypesByNameMap()) {
			(void)lookup_name;
			if (type_info == nullptr || !type_info->isStruct()) {
				continue;
			}
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (struct_info == nullptr || struct_info->declaration_node == nullptr ||
				struct_info->declaration_node->name() != local_name ||
				!struct_info->declaration_node->is_local_class()) {
				continue;
			}
			if (std::find(local_type_infos.begin(), local_type_infos.end(), type_info) ==
				local_type_infos.end()) {
				local_type_infos.push_back(type_info);
			}
		}
		REQUIRE(local_type_infos.size() == 2u);

		CanonicalTypeTable& canonical_types = context.canonicalTypes();
		std::array<EntityId, 2> entities{};
		for (std::size_t index = 0; index < local_type_infos.size(); ++index) {
			const TypeInfo& type_info = *local_type_infos[index];
			const StructTypeInfo* struct_info = type_info.getStructInfo();
			REQUIRE(struct_info != nullptr);
			REQUIRE(struct_info->declaration_node != nullptr);
			const StructDeclarationNode& declaration = *struct_info->declaration_node;
			REQUIRE(declaration.has_entity_id());
			REQUIRE(declaration.has_lexical_scope_id());
			const EntityRecord& entity =
				context.declarationBuilder().entity(declaration.entity_id());
			REQUIRE(isLocalScopeOwnedOwnerId(entity.owner_id));
			CHECK(localScopeFromOwnerId(entity.owner_id) == declaration.lexical_scope_id());
			entities[index] = declaration.entity_id();

			TypeSpecifierNode syntax(
				type_info.registeredTypeIndex().withCategory(TypeCategory::Struct),
				type_info.sizeInBits(),
				Token{},
				CVQualifier::None,
				ReferenceQualifier::None);
			tryBindPublishedTypeEntity(syntax);
			REQUIRE(syntax.has_type_entity());
			CHECK(syntax.type_entity() == declaration.entity_id());
			const CanonicalTypeImport imported =
				importCanonicalType(canonical_types, syntax);
			REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
			CHECK(canonical_types.node(imported.type).kind == CanonicalTypeKind::Record);
			CHECK(canonical_types.recordEntity(imported.type) == declaration.entity_id());

			TypeSpecifierNode pointer_syntax = syntax;
			pointer_syntax.add_pointer_level();
			const CanonicalTypeImport pointer_import =
				importCanonicalType(canonical_types, pointer_syntax);
			REQUIRE(pointer_import.status == CanonicalTypeImportStatus::Supported);
			REQUIRE(canonical_types.node(pointer_import.type).kind == CanonicalTypeKind::Pointer);
			CHECK(canonical_types.node(pointer_import.type).child == imported.type);

			TypeSpecifierNode reference_syntax = syntax;
			reference_syntax.set_reference_qualifier(ReferenceQualifier::LValueReference);
			const CanonicalTypeImport reference_import =
				importCanonicalType(canonical_types, reference_syntax);
			REQUIRE(reference_import.status == CanonicalTypeImportStatus::Supported);
			REQUIRE(canonical_types.node(reference_import.type).kind ==
				CanonicalTypeKind::LValueReference);
			CHECK(canonical_types.node(reference_import.type).child == imported.type);
		}
		CHECK(entities[0] != entities[1]);
	}

	TEST_CASE("Member pointer parameters bind published class EntityId at parse") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();
		FrontendContext context;
		const std::string code =
			"struct Owner { int field; };\n"
			"int read(const Owner& value, int Owner::* member) { return value.*member; }\n"
			"int main() { Owner owner{1}; return read(owner, &Owner::field) - 1; }\n";
		CompileContext test_context;
		test_context.setInputFile("member_pointer_entityid_binding.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());
		bool found_member_object_pointer = false;
		const CanonicalTypeTable& types = context.canonicalTypes();
		for (uint32_t index = 1; index <= types.size(); ++index) {
			if (types.node(TypeId{index}).kind == CanonicalTypeKind::MemberObjectPointer) {
				found_member_object_pointer = true;
				break;
			}
		}
		CHECK(found_member_object_pointer);
		CHECK(context.declarationBuilder().entityKindCounts()[
			static_cast<std::size_t>(DeclKind::Class)] >= 1u);
	}

	TEST_CASE("DeclarationBuilder merges compatible function redeclaration into one EntityId") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_redecl");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{11},
			TelemetryTypeId{21},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult created = builder.publishFunction(first, table);
		REQUIRE(created.status == PublishStatus::Created);

		const FunctionDeclRequest second = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{11},
			TelemetryTypeId{21},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder.publishFunction(second, table);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK(merged.entity_id == created.entity_id);
		CHECK(merged.decl_id.value == 2u);
		CHECK(builder.declarationCount() == 2u);
		CHECK(builder.entityCount() == 1u);

		const DeclarationRecord& second_decl = builder.declaration(merged.decl_id);
		CHECK(second_decl.previous_decl_id == created.decl_id);
		const EntityRecord& entity = builder.entity(merged.entity_id);
		CHECK(entity.first_decl_id == created.decl_id);
		CHECK(entity.latest_decl_id == merged.decl_id);
		CHECK((entity.flags & DeclarationFlags::IsDefinition) != 0);
	}

	TEST_CASE("DeclarationBuilder merges compatible declarations across reopened namespace blocks") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle ns_name = StringTable::getOrInternStringHandle("DeclBuilderNsReopen");
		const NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		REQUIRE(ns_handle.isValid());

		table.enter_namespace(ns_handle);
		const ScopeId first_block = table.currentScopeId();
		const StringHandle fn_name = StringTable::getOrInternStringHandle("reopened_ns_fn");
		const FunctionDeclRequest declaration = makeFunctionDeclRequest(
			first_block,
			fn_name,
			TelemetryTypeId{60},
			TelemetryTypeId{70},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult created = builder.publishFunction(declaration, table);
		REQUIRE(created.status == PublishStatus::Created);

		table.exit_scope();
		table.enter_namespace(ns_handle);
		const ScopeId second_block = table.currentScopeId();
		REQUIRE(first_block != second_block);

		const FunctionDeclRequest definition = makeFunctionDeclRequest(
			second_block,
			fn_name,
			TelemetryTypeId{60},
			TelemetryTypeId{70},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder.publishFunction(definition, table);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK(merged.entity_id == created.entity_id);
		CHECK(builder.entityCount() == 1u);
		CHECK(builder.declaration(created.decl_id).lexical_scope_id == first_block);
		CHECK(builder.declaration(merged.decl_id).lexical_scope_id == second_block);
		CHECK(builder.declaration(created.decl_id).lexical_scope_id != builder.declaration(merged.decl_id).lexical_scope_id);
	}

	TEST_CASE("DeclarationBuilder keeps distinct entities across different namespace owners") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle ns_a_name = StringTable::getOrInternStringHandle("DeclBuilderOwnerA");
		const StringHandle ns_b_name = StringTable::getOrInternStringHandle("DeclBuilderOwnerB");
		const NamespaceHandle ns_a = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_a_name);
		const NamespaceHandle ns_b = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_b_name);
		REQUIRE(ns_a.isValid());
		REQUIRE(ns_b.isValid());
		REQUIRE(ns_a != ns_b);

		const StringHandle fn_name = StringTable::getOrInternStringHandle("same_owner_fn");
		table.enter_namespace(ns_a);
		const ScopeId scope_a = table.currentScopeId();
		const PublishResult a = builder.publishFunction(
			makeFunctionDeclRequest(scope_a, fn_name, TelemetryTypeId{61}, TelemetryTypeId{71}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		table.exit_scope();

		table.enter_namespace(ns_b);
		const ScopeId scope_b = table.currentScopeId();
		const PublishResult b = builder.publishFunction(
			makeFunctionDeclRequest(scope_b, fn_name, TelemetryTypeId{61}, TelemetryTypeId{71}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);

		CHECK(a.status == PublishStatus::Created);
		CHECK(b.status == PublishStatus::Created);
		CHECK(a.entity_id != b.entity_id);
		CHECK(builder.entity(a.entity_id).owner_id == ownerIdFromNamespaceHandle(ns_a));
		CHECK(builder.entity(b.entity_id).owner_id == ownerIdFromNamespaceHandle(ns_b));
		CHECK(builder.entityCount() == 2u);
	}

	TEST_CASE("DeclarationBuilder rejects duplicate function definition without committing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_dup_def");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{12},
			TelemetryTypeId{22},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(first, table).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();
		const std::size_t entities = builder.entityCount();

		const FunctionDeclRequest second = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{12},
			TelemetryTypeId{22},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult rejected = builder.publishFunction(second, table);
		CHECK(rejected.status == PublishStatus::Rejected);
		CHECK(rejected.entity_id.value == 1u);
		CHECK_FALSE(rejected.decl_id);
		CHECK(builder.declarationCount() == decls);
		CHECK(builder.entityCount() == entities);
	}

	TEST_CASE("DeclarationBuilder creates separate entities for C++ overloads") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_overload");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{31},
			TelemetryTypeId{41},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const FunctionDeclRequest second = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{32},
			TelemetryTypeId{41},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult a = builder.publishFunction(first, table);
		const PublishResult b = builder.publishFunction(second, table);
		CHECK(a.status == PublishStatus::Created);
		CHECK(b.status == PublishStatus::Created);
		CHECK(a.entity_id != b.entity_id);
		CHECK(builder.entityCount() == 2u);
	}

	TEST_CASE("DeclarationBuilder rejects return-type conflict on same signature") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_ret_conflict");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{33},
			TelemetryTypeId{50},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(first, table).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();

		const FunctionDeclRequest conflict = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{33},
			TelemetryTypeId{51},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult rejected = builder.publishFunction(conflict, table);
		CHECK(rejected.status == PublishStatus::Rejected);
		CHECK(builder.declarationCount() == decls);
	}

	TEST_CASE("DeclarationBuilder requires matching constexpr on redeclaration") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_constexpr");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{34},
			TelemetryTypeId{52},
			FunctionDeclForm::ConstexprDeclaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(first, table).status == PublishStatus::Created);

		const FunctionDeclRequest mismatch = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{34},
			TelemetryTypeId{52},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(mismatch, table).status == PublishStatus::Rejected);

		const FunctionDeclRequest match = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{34},
			TelemetryTypeId{52},
			FunctionDeclForm::ConstexprDefinition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder.publishFunction(match, table);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK((builder.entity(merged.entity_id).flags & DeclarationFlags::IsInline) != 0);
		CHECK((builder.entity(merged.entity_id).flags & DeclarationFlags::IsConstexpr) != 0);
	}

	TEST_CASE("DeclarationBuilder rejects inline after non-inline definition") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_inline_order");
		const FunctionDeclRequest definition = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{35},
			TelemetryTypeId{53},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(definition, table).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();

		const FunctionDeclRequest inline_after = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{35},
			TelemetryTypeId{53},
			FunctionDeclForm::InlineDeclaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(inline_after, table).status == PublishStatus::Rejected);
		CHECK(builder.declarationCount() == decls);

		FrontendContext context2;
		DeclarationBuilder& builder2 = context2.declarationBuilder();
		SymbolTable table2;
		const ScopeId global_scope2 = table2.currentScopeId();
		const FunctionDeclRequest inline_first = makeFunctionDeclRequest(
			global_scope2,
			name,
			TelemetryTypeId{35},
			TelemetryTypeId{53},
			FunctionDeclForm::InlineDeclaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder2.publishFunction(inline_first, table2).status == PublishStatus::Created);
		const FunctionDeclRequest definition_after = makeFunctionDeclRequest(
			global_scope2,
			name,
			TelemetryTypeId{35},
			TelemetryTypeId{53},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder2.publishFunction(definition_after, table2);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK((builder2.entity(merged.entity_id).flags & DeclarationFlags::IsInline) != 0);
		CHECK((builder2.entity(merged.entity_id).flags & DeclarationFlags::IsDefinition) != 0);
	}

	TEST_CASE("DeclarationBuilder rejects nonexistent and invalid-kind publication scopes") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_invalid_scope");
		const FunctionDeclRequest base = makeFunctionDeclRequest(
			table.currentScopeId(),
			name,
			TelemetryTypeId{37},
			TelemetryTypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		const FunctionDeclRequest missing_scope = makeFunctionDeclRequest(
			ScopeId{999},
			name,
			TelemetryTypeId{37},
			TelemetryTypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK_THROWS_AS(builder.publishFunction(missing_scope, table), InternalError);

		const FunctionDeclRequest absent_scope = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TelemetryTypeId{37},
			TelemetryTypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(absent_scope, table).status == PublishStatus::Rejected);

		table.enter_scope(ScopeType::Block);
		const FunctionDeclRequest block_scope = makeFunctionDeclRequest(
			table.currentScopeId(),
			name,
			TelemetryTypeId{37},
			TelemetryTypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(block_scope, table).status == PublishStatus::Rejected);
		table.exit_scope();

		table.enter_scope(ScopeType::Function);
		const FunctionDeclRequest function_scope = makeFunctionDeclRequest(
			table.currentScopeId(),
			name,
			TelemetryTypeId{37},
			TelemetryTypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(function_scope, table).status == PublishStatus::Rejected);
		table.exit_scope();

		CHECK(builder.publishFunction(base, table).status == PublishStatus::Created);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
	}

	TEST_CASE("DeclarationBuilder rejects invalid requests without committing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_invalid");
		const FunctionDeclRequest invalid_scope = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TelemetryTypeId{37},
			TelemetryTypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(invalid_scope, table).status == PublishStatus::Rejected);
		CHECK(builder.declarationCount() == 0u);
		CHECK(builder.entityCount() == 0u);
	}

	TEST_CASE("PreparedFunctionPublication cannot be fabricated by callers") {
		static_assert(!std::is_default_constructible_v<PreparedFunctionPublication>);
		static_assert(!std::is_copy_constructible_v<PreparedFunctionPublication>);
		static_assert(!std::is_copy_assignable_v<PreparedFunctionPublication>);
		static_assert(!std::is_constructible_v<
			PreparedFunctionPublication,
			PublishStatus,
			EntityId,
			ScopeId,
			OwnerId,
			StringHandle,
			TelemetryTypeId,
			TelemetryTypeId,
			uint8_t>);
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication matches publishFunction") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_prepare");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{71},
			TelemetryTypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(first, table);
		CHECK_FALSE(prepared.isRejected());
		PublishResult committed{};
		{
			PublicationTransaction first_transaction(builder);
			PreparedFunctionPublication to_commit = builder.prepareFunctionPublication(first, table);
			committed = builder.commitFunctionPublication(to_commit, first_transaction);
			first_transaction.commit();
		}
		CHECK(committed.status == PublishStatus::Created);

		const FunctionDeclRequest redecl = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{71},
			TelemetryTypeId{81},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared_redecl =
			builder.prepareFunctionPublication(redecl, table);
		CHECK_FALSE(prepared_redecl.isRejected());
		const PublishResult committed_redecl = builder.publishFunction(redecl, table);
		CHECK(committed_redecl.status == PublishStatus::MergedRedeclaration);
	}

	TEST_CASE("PublicationTransaction rollback restores merged entity state") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_txn_merge");
		const FunctionDeclRequest decl = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{92},
			TelemetryTypeId{102},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult created = builder.publishFunction(decl, table);
		REQUIRE(created.status == PublishStatus::Created);
		const EntityRecord entity_before = builder.entity(created.entity_id);

		const FunctionDeclRequest definition = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{92},
			TelemetryTypeId{102},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		PublicationTransaction transaction(builder);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(definition, table);
		REQUIRE_FALSE(prepared.isRejected());
		const PublishResult merged = builder.commitFunctionPublication(prepared, transaction);
		REQUIRE(merged.status == PublishStatus::MergedRedeclaration);
		CHECK(builder.declarationCount() == 2u);
		CHECK(builder.entity(created.entity_id).latest_decl_id == merged.decl_id);
		transaction.rollback();

		CHECK(builder.declarationCount() == 1u);
		const EntityRecord entity_after = builder.entity(created.entity_id);
		CHECK(entity_after.latest_decl_id == entity_before.latest_decl_id);
		CHECK(entity_after.flags == entity_before.flags);
	}

	TEST_CASE("PublicationTransaction rollback restores inserted parameter-list signatures") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		const std::size_t parameter_lists_before = builder.telemetryParameterListInternCount();

		PublicationTransaction transaction(builder);
		const TelemetryTypeId signature_id =
			builder.internParameterListSignature(std::span<const ASTNode>{}, false, &transaction);
		CHECK(signature_id.value >= 1u);
		transaction.rollback();

		CHECK(builder.telemetryParameterListInternCount() == parameter_lists_before);
	}

	TEST_CASE("PublicationTransaction rollback restores declaration and entity arenas") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_txn");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{91},
			TelemetryTypeId{101},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		PublicationTransaction transaction(builder);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
		REQUIRE_FALSE(prepared.isRejected());
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
		transaction.rollback();
		CHECK(builder.declarationCount() == 0u);
		CHECK(builder.entityCount() == 0u);
	}

	TEST_CASE("PublicationTransaction rollback restores telemetry intern registries") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		TypeSpecifierNode int_type;
		int_type.set_category(TypeCategory::Int);
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_intern_txn");

		PublicationTransaction transaction(builder);
		const TelemetryTypeId return_id = builder.internDeclaratorType(int_type);
		const FunctionDeclRequest invalid = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TelemetryTypeId{200},
			return_id,
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.prepareFunctionPublication(invalid, table).isRejected());
		transaction.rollback();

		const TelemetryTypeId second_return_id = builder.internDeclaratorType(int_type);
		CHECK(second_return_id.value == 1u);
	}

	TEST_CASE("PublicationTransaction nested commit remains provisional") {
		FrontendContext context;
		auto& builder = context.declarationBuilder();
		const TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
		PublicationTransaction outer(builder);
		{
			PublicationTransaction inner(builder);
			builder.internDeclaratorType(integer);
			inner.commit();
		}
		CHECK(builder.telemetryDeclaratorInternCount() == 1);
		outer.rollback();
		CHECK(builder.telemetryDeclaratorInternCount() == 0);
		CHECK(context.canonicalTypes().size() == 0);
	}

	TEST_CASE("PublicationTransaction rolls back on stack unwinding") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_txn_unwind");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{93},
			TelemetryTypeId{103},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		try {
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
			throw std::runtime_error("decl_builder_publication_unwind_probe");
		} catch (const std::runtime_error&) {
		}

		CHECK(builder.declarationCount() == 0u);
		CHECK(builder.entityCount() == 0u);
	}

	TEST_CASE("PublicationTransaction rollback restores two created and two merged publications") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle merge_a = StringTable::getOrInternStringHandle("txn_multi_merge_a");
		const StringHandle merge_b = StringTable::getOrInternStringHandle("txn_multi_merge_b");
		const StringHandle create_a = StringTable::getOrInternStringHandle("txn_multi_create_a");
		const StringHandle create_b = StringTable::getOrInternStringHandle("txn_multi_create_b");

		const PublishResult first_a = builder.publishFunction(
			makeFunctionDeclRequest(global_scope, merge_a, TelemetryTypeId{201}, TelemetryTypeId{301}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		const PublishResult first_b = builder.publishFunction(
			makeFunctionDeclRequest(global_scope, merge_b, TelemetryTypeId{202}, TelemetryTypeId{302}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		REQUIRE(first_a.status == PublishStatus::Created);
		REQUIRE(first_b.status == PublishStatus::Created);
		const EntityRecord entity_a_before = builder.entity(first_a.entity_id);
		const EntityRecord entity_b_before = builder.entity(first_b.entity_id);
		const std::size_t decls_before = builder.declarationCount();
		const std::size_t entities_before = builder.entityCount();

		{
			PublicationTransaction outer(builder);
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication created_prep_a = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, create_a, TelemetryTypeId{203}, TelemetryTypeId{303}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
				table);
			PreparedFunctionPublication created_prep_b = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, create_b, TelemetryTypeId{204}, TelemetryTypeId{304}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
				table);
			PreparedFunctionPublication merged_prep_a = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, merge_a, TelemetryTypeId{201}, TelemetryTypeId{301}, FunctionDeclForm::Definition, LanguageLinkage::CPlusPlus),
				table);
			PreparedFunctionPublication merged_prep_b = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, merge_b, TelemetryTypeId{202}, TelemetryTypeId{302}, FunctionDeclForm::Definition, LanguageLinkage::CPlusPlus),
				table);
			REQUIRE_FALSE(created_prep_a.isRejected());
			REQUIRE_FALSE(created_prep_b.isRejected());
			REQUIRE_FALSE(merged_prep_a.isRejected());
			REQUIRE_FALSE(merged_prep_b.isRejected());
			REQUIRE(builder.commitFunctionPublication(created_prep_a, transaction).status == PublishStatus::Created);
			REQUIRE(builder.commitFunctionPublication(created_prep_b, transaction).status == PublishStatus::Created);
			REQUIRE(builder.commitFunctionPublication(merged_prep_a, transaction).status ==
					PublishStatus::MergedRedeclaration);
			REQUIRE(builder.commitFunctionPublication(merged_prep_b, transaction).status ==
					PublishStatus::MergedRedeclaration);
			CHECK(builder.declarationCount() == decls_before + 4u);
			CHECK(builder.entityCount() == entities_before + 2u);
			transaction.commit();
			outer.rollback();
		}

		CHECK(builder.declarationCount() == decls_before);
		CHECK(builder.entityCount() == entities_before);
		CHECK(builder.entity(first_a.entity_id).latest_decl_id == entity_a_before.latest_decl_id);
		CHECK(builder.entity(first_a.entity_id).flags == entity_a_before.flags);
		CHECK(builder.entity(first_b.entity_id).latest_decl_id == entity_b_before.latest_decl_id);
		CHECK(builder.entity(first_b.entity_id).flags == entity_b_before.flags);

		const PublishResult recreate_a = builder.publishFunction(
			makeFunctionDeclRequest(global_scope, create_a, TelemetryTypeId{203}, TelemetryTypeId{303}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		CHECK(recreate_a.status == PublishStatus::Created);
		CHECK(recreate_a.entity_id != first_a.entity_id);
		CHECK(recreate_a.entity_id != first_b.entity_id);
	}

	TEST_CASE("DeclarationBuilder rejects committing the same prepared publication twice") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_double_commit");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{205},
			TelemetryTypeId{305},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
		REQUIRE_FALSE(prepared.isRejected());
		PublicationTransaction transaction(builder);
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();
		const std::size_t entities = builder.entityCount();

		bool threw = false;
		try {
			builder.commitFunctionPublication(prepared, transaction);
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
		CHECK(builder.declarationCount() == decls);
		CHECK(builder.entityCount() == entities);
		transaction.commit();
	}

	TEST_CASE("PublicationTransaction checkpoint stays bounded as builder grows") {
		static_assert(sizeof(DeclarationBuilderCheckpoint) <= 32);

		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();

		for (uint32_t index = 0; index < 128u; ++index) {
			const StringHandle name = StringTable::getOrInternStringHandle(
				StringBuilder().append("decl_builder_scale_").append(static_cast<int64_t>(index)));
			const FunctionDeclRequest request = makeFunctionDeclRequest(
				global_scope,
				name,
				TelemetryTypeId{index + 1u},
				TelemetryTypeId{index + 1000u},
				FunctionDeclForm::Declaration,
				LanguageLinkage::CPlusPlus);
			REQUIRE(builder.publishFunction(request, table).status == PublishStatus::Created);
		}

		CHECK(builder.declarationCount() == 128u);
		CHECK(builder.entityCount() == 128u);

		const StringHandle rollback_name =
			StringTable::getOrInternStringHandle("decl_builder_scale_rollback");
		PublicationTransaction transaction(builder);
		const FunctionDeclRequest rollback_request = makeFunctionDeclRequest(
			global_scope,
			rollback_name,
			TelemetryTypeId{9999},
			TelemetryTypeId{10000},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared =
			builder.prepareFunctionPublication(rollback_request, table);
		REQUIRE_FALSE(prepared.isRejected());
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
		transaction.rollback();

		CHECK(builder.declarationCount() == 128u);
		CHECK(builder.entityCount() == 128u);
	}

	TEST_CASE("Parser publication reject retains SymbolTable insertion") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
int symtab_undo_conflict_row(int value);
float symtab_undo_conflict_row(int value);
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("symtab_insert_undo_conflict_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() == 1u);
		CHECK(context.entityCount() == 1u);

		const StringHandle name = StringTable::getOrInternStringHandle("symtab_undo_conflict_row");
		const std::vector<ASTNode> overloads =
			gSymbolTable.lookup_all(StringTable::getStringView(name));
		CHECK(overloads.size() == 2u);
	}

	TEST_CASE("commitParserFreeFunctionPublication rejects duplicate definitions without committing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_commit_dup");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{111},
			TelemetryTypeId{121},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		{
			PublicationTransaction first_transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(first, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, first_transaction).status ==
					PublishStatus::Created);
			first_transaction.commit();
		}
		const std::size_t decls = builder.declarationCount();
		const std::size_t entities = builder.entityCount();
		const std::size_t declarator_interns = builder.telemetryDeclaratorInternCount();
		const std::size_t parameter_lists = builder.telemetryParameterListInternCount();

		const FunctionDeclRequest duplicate = makeFunctionDeclRequest(
			global_scope,
			name,
			TelemetryTypeId{111},
			TelemetryTypeId{121},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		PublicationTransaction reject_transaction(builder);
		CHECK(builder.prepareFunctionPublication(duplicate, table).isRejected());
		reject_transaction.rollback();

		CHECK(builder.declarationCount() == decls);
		CHECK(builder.entityCount() == entities);
		CHECK(builder.telemetryDeclaratorInternCount() == declarator_interns);
		CHECK(builder.telemetryParameterListInternCount() == parameter_lists);
	}

	TEST_CASE("Parser shadow publication rejects duplicate definition without builder commit") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
void decl_builder_dup_shadow();
void decl_builder_dup_shadow() {}
void decl_builder_dup_shadow() {}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_dup_shadow_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() == 2u);
		CHECK(context.entityCount() == 1u);
	}

	TEST_CASE("buildNamespaceHandleForStructName rejects unregistered qualified spelling") {
		StringHandle qualified = StringTable::getOrInternStringHandle("ns::Widget");
		NamespaceHandle handle = buildNamespaceHandleForStructName(qualified);
		CHECK_FALSE(handle.isValid());
	}

	TEST_CASE("Parser publishes namespace free functions through DeclarationBuilder shadow path") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
namespace decl_builder_wire_parser_ns {
void wire_shadow_fn();
void wire_shadow_fn() {}
}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_wire_parser_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() == 2u);
		CHECK(context.entityCount() == 1u);
	}

	TEST_CASE("Parser shadow publication merges inline declarations across reopened namespace blocks") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
namespace decl_builder_reopened_inline_ns {
inline int reopened_inline_fn();
}
namespace decl_builder_reopened_inline_ns {
int reopened_inline_fn() { return 7; }
}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_reopened_inline_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		REQUIRE(context.declarationCount() == 2u);
		REQUIRE(context.entityCount() == 1u);

		DeclarationBuilder& builder = context.declarationBuilder();
		const DeclarationRecord& declaration = builder.declaration(DeclId{1});
		const DeclarationRecord& definition = builder.declaration(DeclId{2});
		const EntityRecord& entity = builder.entity(EntityId{1});
		CHECK(declaration.entity_id == EntityId{1});
		CHECK(definition.entity_id == EntityId{1});
		CHECK(definition.previous_decl_id == declaration.id);
		CHECK(declaration.lexical_scope_id != definition.lexical_scope_id);
		CHECK(entity.owner_id);
		CHECK(entity.first_decl_id == declaration.id);
		CHECK(entity.latest_decl_id == definition.id);
		CHECK((entity.flags & DeclarationFlags::IsInline) != 0);
		CHECK((entity.flags & DeclarationFlags::IsDefinition) != 0);
	}

	TEST_CASE("Parser DeclarationBuilder shadow path keeps valid overloads parsing") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		// Valid overloads: array-bound distinctions in reference parameters.
		// The telemetry interner may collapse them, but parse must not fail.
		const std::string code = R"(
int decl_builder_wire_check_row(int (&)[3]) { return 0; }
int decl_builder_wire_check_row(int (&)[2]) { return 1; }
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_wire_overload_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() >= 1u);
		CHECK(context.entityCount() >= 1u);
	}
}
TEST_SUITE("Diagnostics") {
	TEST_CASE("Diagnostic ID names and identity stay stable independent of message text") {
		CHECK(diagnosticIdName(DiagnosticId::PointerToReferenceType) == "PointerToReferenceType");
		CHECK(diagnosticIdName(DiagnosticId::NoteToMatchOpeningBracket) == "NoteToMatchOpeningBracket");
		CHECK(diagnosticIdName(DiagnosticId::DecltypeAutoCvQualifier) == "DecltypeAutoCvQualifier");
		CHECK(diagnosticIdName(DiagnosticId::DecltypeAutoPointerOrReference) == "DecltypeAutoPointerOrReference");
		CHECK(diagnosticIdName(DiagnosticId::DecltypeAutoStructuredBinding) == "DecltypeAutoStructuredBinding");
		CHECK(diagnosticIdName(DiagnosticId::ParameterPackDataMember) == "ParameterPackDataMember");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointModuloOperator) == "FloatingPointModuloOperator");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointBitwiseCompoundAssignment) == "FloatingPointBitwiseCompoundAssignment");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointShiftOperator) == "FloatingPointShiftOperator");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointBitwiseOperator) == "FloatingPointBitwiseOperator");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousOperatorOverload) == "AmbiguousOperatorOverload");
		CHECK(diagnosticIdName(DiagnosticId::StaticOperatorMustBeNonStaticMember) == "StaticOperatorMustBeNonStaticMember");
		CHECK(diagnosticIdName(DiagnosticId::OperatorDefaultArgumentsForbidden) == "OperatorDefaultArgumentsForbidden");
		CHECK(diagnosticIdName(DiagnosticId::AssignmentOperatorArity) == "AssignmentOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::SubscriptOperatorArity) == "SubscriptOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::ArrowOperatorArity) == "ArrowOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::IncrementDecrementOperatorForm) == "IncrementDecrementOperatorForm");
		CHECK(diagnosticIdName(DiagnosticId::OrdinaryOperatorArity) == "OrdinaryOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::DeletedCopyAssignment) == "DeletedCopyAssignment");
		CHECK(diagnosticIdName(DiagnosticId::DeletedMoveAssignment) == "DeletedMoveAssignment");
		CHECK(diagnosticIdName(DiagnosticId::ImmediateInvocationNotConstant) == "ImmediateInvocationNotConstant");
		CHECK(diagnosticIdName(DiagnosticId::DeletedCopyConstructor) == "DeletedCopyConstructor");
		CHECK(diagnosticIdName(DiagnosticId::DeletedMoveConstructor) == "DeletedMoveConstructor");
		CHECK(diagnosticIdName(DiagnosticId::AssignmentToConstObject) == "AssignmentToConstObject");
		CHECK(diagnosticIdName(DiagnosticId::OperatorOverloadNotFound) == "OperatorOverloadNotFound");
		CHECK(diagnosticIdName(DiagnosticId::DeletedOperatorFunction) == "DeletedOperatorFunction");
		CHECK(diagnosticIdName(DiagnosticId::ConstinitInitializerNotConstant) == "ConstinitInitializerNotConstant");
		CHECK(diagnosticIdName(DiagnosticId::ConstexprStaticMemberInitializerNotConstant) == "ConstexprStaticMemberInitializerNotConstant");
		CHECK(diagnosticIdName(DiagnosticId::ExplicitConstructorCopyInitialization) == "ExplicitConstructorCopyInitialization");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousConstructorCall) == "AmbiguousConstructorCall");
		CHECK(diagnosticIdName(DiagnosticId::RangeForBeginEndRequired) == "RangeForBeginEndRequired");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousDerivedToBasePointerConversion) == "AmbiguousDerivedToBasePointerConversion");
		CHECK(diagnosticIdName(DiagnosticId::InaccessibleDerivedToBasePointerConversion) == "InaccessibleDerivedToBasePointerConversion");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousBuiltInSubscriptConversion) == "AmbiguousBuiltInSubscriptConversion");
		CHECK(diagnosticIdName(DiagnosticId::UnexpectedToken) == "UnexpectedToken");
		CHECK(diagnosticIdName(DiagnosticId::MissingSemicolon) == "MissingSemicolon");
		CHECK(diagnosticIdName(DiagnosticId::UnexpectedEndOfFile) == "UnexpectedEndOfFile");

		// Same ID must serve different message templates without identity drift.
		DiagnosticEngine engine;
		uint32_t first = engine.report(
			DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
			SourceLocation::fromParts(1, 1, 0), "template one {}", {});
		uint32_t second = engine.report(
			DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
			SourceLocation::fromParts(2, 1, 0), "totally different text", {});
		CHECK(engine.diagnostic(first).id == engine.diagnostic(second).id);
	}

	TEST_CASE("Severity classification accumulates per level and flags errors") {
		DiagnosticEngine engine;
		engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Warning,
					  SourceLocation::fromParts(1, 1, 0), "w", {});
		engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
					  SourceLocation::fromParts(2, 1, 0), "e", {});
		engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
					  SourceLocation::fromParts(3, 1, 0), "e2", {});
		engine.report(DiagnosticId::None, DiagnosticSeverity::Note,
					  SourceLocation::fromParts(4, 1, 0), "n", {});

		CHECK(engine.count(DiagnosticSeverity::Warning) == 1);
		CHECK(engine.count(DiagnosticSeverity::Error) == 2);
		CHECK(engine.count(DiagnosticSeverity::Note) == 1);
		CHECK(engine.diagnostics().size() == 4);
		CHECK(engine.hasErrors());
		CHECK(diagnosticSeverityTag(DiagnosticSeverity::Fatal) == std::string_view("fatal error"));

		DiagnosticEngine clean_engine;
		CHECK_FALSE(clean_engine.hasErrors());
	}

	TEST_CASE("Locations and ranges are stored verbatim") {
		DiagnosticEngine engine;
		SourceRange range = SourceRange::fromLocations(
			SourceLocation::fromParts(7, 9, 1), SourceLocation::fromParts(7, 14, 1));
		uint32_t index = engine.reportWithRange(
			DiagnosticId::ExpectedCloseBracketAfterArraySize, DiagnosticSeverity::Error,
			SourceLocation::fromParts(7, 14, 1), range, "missing bracket", {});

		const Diagnostic& stored = engine.diagnostic(index);
		CHECK(stored.location.line == 7u);
		CHECK(stored.location.column == 14u);
		CHECK(stored.location.file_index == 1u);
		REQUIRE(stored.has_range());
		CHECK(stored.range.begin.column == 9u);
		CHECK(stored.range.end.column == 14u);

		// Compact record check: locations are 12 bytes (three uint32 fields).
		MESSAGE("sizeof(SourceLocation)=" << sizeof(SourceLocation)
				<< " sizeof(SourceRange)=" << sizeof(SourceRange));
		CHECK(sizeof(SourceLocation) == 12u);
	}

	TEST_CASE("Arguments render sequentially into template placeholders") {
		StringHandle interned = StringTable::getOrInternStringHandle("InternedName");
		const std::array<DiagnosticArgument, 3> mixed_arguments{
			DiagnosticArgument::text("x"),
			DiagnosticArgument::internedText(interned),
			DiagnosticArgument::unsignedInteger(2)};
		std::string rendered = renderDiagnosticMessage(
			"suffixes on declarator '{}' at {} are not supported, count {}",
			mixed_arguments);
		CHECK(rendered == "suffixes on declarator 'x' at InternedName are not supported, count 2");

		const std::array<DiagnosticArgument, 1> negative_argument{
			DiagnosticArgument::signedInteger(-42)};
		std::string signed_render = renderDiagnosticMessage(
			"line {}", negative_argument);
		CHECK(signed_render == "line -42");

		// Contract: a missing argument renders nothing for its placeholder.
		const std::array<DiagnosticArgument, 1> present_argument{
			DiagnosticArgument::unsignedInteger(1)};
		std::string missing = renderDiagnosticMessage(
			"value {}", present_argument);
		CHECK(missing == "value 1");
	}

	TEST_CASE("Accumulated diagnostics own dynamic templates and text arguments") {
		DiagnosticEngine engine;
		uint32_t index = 0;
		{
			std::string message_template = "dynamic {}";
			std::string argument_text = "before";
			std::string note_template = "note {}";
			std::string note_text = "detail";
			const std::array<DiagnosticArgument, 1> arguments{
				DiagnosticArgument::text(argument_text)};
			const std::array<DiagnosticArgument, 1> note_arguments{
				DiagnosticArgument::text(note_text)};
			index = engine.report(
				DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
				SourceLocation::fromParts(1, 1, 0), message_template, arguments);
			engine.attachNote(
				index, DiagnosticId::NoteToMatchOpeningBracket,
				SourceLocation::fromParts(1, 2, 0), note_template, note_arguments);

			// Keep the allocations alive but overwrite their bytes. A borrowed
			// string_view deterministically observes these mutations.
			message_template.assign("XXXXXXXXXX");
			argument_text.assign("mutate");
			note_template.assign("YYYYYYY");
			note_text.assign("change");
		}

		std::deque<std::string> paths{"owned.cpp"};
		std::string rendered = renderDiagnostic(engine.diagnostic(index), engine, paths);
		CHECK(rendered.find("dynamic before") != std::string::npos);
		CHECK(rendered.find("note: note detail") != std::string::npos);
	}

	TEST_CASE("Notes attach through pool indices and render under the parent") {
		DiagnosticEngine engine;
		uint32_t index = engine.reportWithRange(
			DiagnosticId::ExpectedCloseBracketAfterArraySize, DiagnosticSeverity::Error,
			SourceLocation::fromParts(3, 35, 0),
			SourceRange::fromLocations(SourceLocation::fromParts(3, 32, 0), SourceLocation::fromParts(3, 35, 0)),
			"Expected ']' after array size", {});
		engine.attachNote(index, DiagnosticId::NoteToMatchOpeningBracket,
						  SourceLocation::fromParts(3, 32, 0), "to match this '['", {});

		const Diagnostic& parent = engine.diagnostic(index);
		REQUIRE(parent.note_indices.size() == 1);
		CHECK(engine.note(parent.note_indices[0]).id == DiagnosticId::NoteToMatchOpeningBracket);

		std::deque<std::string> paths{"unit.cpp"};
		std::string rendered = renderDiagnostic(parent, engine, paths);
		CHECK(rendered.find("unit.cpp:3:35: error: Expected ']' after array size [ExpectedCloseBracketAfterArraySize#1003]") != std::string::npos);
		CHECK(rendered.find("unit.cpp:3:32: note: to match this '[' [NoteToMatchOpeningBracket#1051]") != std::string::npos);
	}

	TEST_CASE("Rendered lines expose machine-consumable id, line, and column") {
		DiagnosticEngine engine;
		uint32_t index = engine.report(
			DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
			SourceLocation::fromParts(2, 30, 0), "Cannot form a pointer to reference type", {});
		std::deque<std::string> paths{"m.cpp"};
		std::string rendered = renderDiagnostic(engine.diagnostic(index), engine, paths);

		// Contract: deterministic location prefix and trailing stable-id tag;
		// both parseable without message prose.
		CHECK(rendered.compare(0, 18, "m.cpp:2:30: error:") == 0);
		const std::string expected_suffix = "[PointerToReferenceType#1001]";
		REQUIRE(rendered.size() > expected_suffix.size());
		CHECK(rendered.substr(rendered.size() - expected_suffix.size()) == expected_suffix);
		CHECK(diagnosticIdNumber(DiagnosticId::ExpectedCloseBracketAfterArraySize) == 1003u);
	}

	TEST_CASE("Template-instantiation context snapshots by value at report time") {
		DiagnosticEngine engine;
		StringHandle outer = StringTable::getOrInternStringHandle("Outer<int>");
		StringHandle inner = StringTable::getOrInternStringHandle("Inner::func");
		{
			DiagnosticEngine::TemplateContextGuard outer_guard(engine, outer, SourceLocation::fromParts(10, 5, 0));
			{
				DiagnosticEngine::TemplateContextGuard inner_guard(engine, inner, SourceLocation{});
				CHECK(engine.templateContextDepth() == 2);
				engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
							  SourceLocation::fromParts(11, 1, 0), "boom", {});
			}
			CHECK(engine.templateContextDepth() == 1);
		}
		CHECK(engine.templateContextDepth() == 0);

		REQUIRE(engine.diagnostics().size() == 1);
		const TemplateVector<TemplateInstantiationFrame, 4>& frames = engine.diagnostic(0).instantiation_context;
		REQUIRE(frames.size() == 2);
		CHECK(frames[0].template_name == outer);
		CHECK(frames[1].template_name == inner);
	}

	TEST_CASE("CompileError bridge preserves what() and carries structured payload") {
		DiagnosticEngine engine;
		const std::array<DiagnosticArgument, 1> declarator_argument{
			DiagnosticArgument::text("decl")};
		uint32_t index = engine.report(
			DiagnosticId::MultipleAsmSuffixesOnDeclarator, DiagnosticSeverity::Error,
			SourceLocation::fromParts(4, 6, 0),
			"Multiple __asm suffixes on declarator '{}' are not supported",
			declarator_argument);

		try {
			throw CompileError::fromStructuredDiagnostic(engine.diagnostic(index));
		} catch (const CompileError& caught) {
			CHECK(std::string(caught.what()) ==
				  "Multiple __asm suffixes on declarator 'decl' are not supported");
			const Diagnostic* payload = caught.structuredDiagnostic();
			REQUIRE(payload != nullptr);
			CHECK(payload->id == DiagnosticId::MultipleAsmSuffixesOnDeclarator);
			CHECK(payload->location.line == 4u);
			CHECK(payload->location.column == 6u);
		}

		// Copying the exception (throw does this implicitly) keeps the payload.
		try {
			throw CompileError::fromStructuredDiagnostic(engine.diagnostic(index));
		} catch (const std::runtime_error& copied) {
			const CompileError* compile_copy = dynamic_cast<const CompileError*>(&copied);
			REQUIRE(compile_copy != nullptr);
			CHECK(compile_copy->structuredDiagnostic() != nullptr);
			CHECK(std::string(compile_copy->what()).find("'decl'") != std::string::npos);
		}
	}

	TEST_CASE("Legacy construction counts toward outside-engine inventory; bridge does not") {
		uint64_t before = diagnosticsEmittedOutsideEngineCount();
		{
			CompileError legacy("legacy failure path");
			(void)legacy;
		}
		uint64_t after_legacy = diagnosticsEmittedOutsideEngineCount();
		CHECK(after_legacy == before + 1);

		{
			DiagnosticEngine engine;
			uint32_t index = engine.report(
				DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
				SourceLocation::fromParts(1, 1, 0), "structured", {});
			CompileError bridged = CompileError::fromStructuredDiagnostic(engine.diagnostic(index));
			(void)bridged;
		}
		CHECK(diagnosticsEmittedOutsideEngineCount() == after_legacy);
	}

	TEST_CASE("Speculative ParseResult errors do not count as emitted diagnostics") {
		uint64_t before = diagnosticsEmittedOutsideEngineCount();
		ParseResult speculative_error = ParseResult::error(
			"probe rejected", Token(Token::Type::Identifier, std::string_view("probe"), 1, 1, 0));
		REQUIRE(speculative_error.is_error());
		CHECK(diagnosticsEmittedOutsideEngineCount() == before);
	}

	TEST_CASE("Lexer maps diagnostic locations back to original source coordinates") {
		const std::array<SourceLineMapping, 2> line_map{
			SourceLineMapping{0, 40, 0},
			SourceLineMapping{1, 7, 1}};
		const std::deque<std::string> paths{"main.cpp", "included.hpp"};
		Lexer lexer("first\nsecond\n", line_map, paths);
		Token first = lexer.next_token();
		Token second = lexer.next_token();

		SourceLocation first_location = lexer.getSourceLocation(first);
		SourceLocation second_location = lexer.getSourceLocation(second);
		CHECK(first_location.line == 40u);
		CHECK(first_location.file_index == 0u);
		CHECK(second_location.line == 7u);
		CHECK(second_location.file_index == 1u);
		// Token columns follow the existing lexer convention and identify the
		// column immediately after the token spelling.
		CHECK(second_location.column == 7u);
	}

	TEST_CASE("Converted pointer-to-reference diagnostic carries structured payload end to end") {
		std::string_view code = "using R = int&;\nint main(){ int x = sizeof(R(*)); return x; }\n";
		Lexer location_lexer(code);
		SourceLocation expected_pointer_location{};
		while (true) {
			Token token = location_lexer.next_token();
			if (token.value() == "*") {
				expected_pointer_location = location_lexer.getSourceLocation(token);
				break;
			}
			REQUIRE(token.type() != Token::Type::EndOfFile);
		}
		Lexer lexer(code);
		CompileContext local_context;
		SemanticAnalysis parser_sema(local_context, gSymbolTable);
		Parser parser(lexer, local_context, parser_sema);

		bool threw = false;
		try {
			parser.parse();
		} catch (const CompileError& caught) {
			threw = true;
			const Diagnostic* payload = caught.structuredDiagnostic();
			REQUIRE(payload != nullptr);
			CHECK(payload->id == DiagnosticId::PointerToReferenceType);
			CHECK(payload->severity == DiagnosticSeverity::Error);
			CHECK(payload->location.line == expected_pointer_location.line);
			CHECK(payload->location.column == expected_pointer_location.column);
			CHECK(payload->location.file_index == expected_pointer_location.file_index);
			CHECK(std::string(caught.what()) == "Cannot form a pointer to reference type");

			std::deque<std::string> paths{"probe.cpp"};
			std::string rendered = renderDiagnostic(*payload, local_context.diagnostics(), paths);
			CHECK(rendered.find("error: Cannot form a pointer to reference type [PointerToReferenceType#1001]") != std::string::npos);
		}
		CHECK(threw);
	}

	TEST_CASE("Template friend declaration resolves its member primary by identity") {
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
struct FriendPayload {};
template <typename T>
struct WideOwner {
	template <typename U> struct WideBox { U inner; };
};
template <typename T>
struct NarrowOwner {
	template <typename U> struct NarrowBox { U inner; };
};
struct FriendHostWide {
	template <typename T>
	friend struct WideOwner<T>::WideBox<int>;
	int markerWide = 1;
};
struct FriendHostNarrow {
	template <typename T>
	friend struct NarrowOwner<T>::NarrowBox<int>;
	int markerNarrow = 2;
};
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("template_friend_member_identity.cpp");
		Lexer lexer(code);
		SemanticAnalysis sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, sema);
		REQUIRE(!parser.parse().is_error());

		const StructDeclarationNode* wide_host = nullptr;
		const StructDeclarationNode* narrow_host = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (!node.is<StructDeclarationNode>()) {
				continue;
			}
			const StructDeclarationNode& candidate = node.as<StructDeclarationNode>();
			if (candidate.name() ==
				StringTable::getOrInternStringHandle("FriendHostWide")) {
				wide_host = &candidate;
			} else if (candidate.name() ==
				StringTable::getOrInternStringHandle("FriendHostNarrow")) {
				narrow_host = &candidate;
			}
		}
		REQUIRE(wide_host != nullptr);
		REQUIRE(narrow_host != nullptr);

		const std::span<const ASTNode> wide_friends =
			wide_host->friend_declarations();
		REQUIRE(wide_friends.size() == 1u);
		REQUIRE(wide_friends[0].is<FriendDeclarationNode>());
		const FriendDeclarationNode& wide_friend =
			wide_friends[0].as<FriendDeclarationNode>();

		const std::span<const ASTNode> narrow_friends =
			narrow_host->friend_declarations();
		REQUIRE(narrow_friends.size() == 1u);
		REQUIRE(narrow_friends[0].is<FriendDeclarationNode>());
		const FriendDeclarationNode& narrow_friend =
			narrow_friends[0].as<FriendDeclarationNode>();

		// A friend naming member specialization arguments uses the exact-
		// specialization representation, not the legacy all-specializations kind.
		CHECK(wide_friend.kind() == FriendKind::Class);
		CHECK(narrow_friend.kind() == FriendKind::Class);

		// The member primary resolves by identity: both declarations name a
		// distinct member template, and the same-spelling-bound primaries are
		// different AST nodes with different owner-derived template names.
		REQUIRE(wide_friend.class_declaration() != nullptr);
		REQUIRE(narrow_friend.class_declaration() != nullptr);
		CHECK(wide_friend.class_declaration() != narrow_friend.class_declaration());
		CHECK(StringTable::getStringView(wide_friend.class_template_name()) ==
			"WideOwner::WideBox"sv);
		CHECK(StringTable::getStringView(narrow_friend.class_template_name()) ==
			"NarrowOwner::NarrowBox"sv);

		// The granted specialization arguments are retained.
		REQUIRE(wide_friend.class_template_arguments().size() == 1u);
		REQUIRE(narrow_friend.class_template_arguments().size() == 1u);
	}

	TEST_CASE("Record size inventory for stored diagnostic records") {
		// Cold-path records; sizes documented here so future layout changes
		// are conscious decisions rather than accidents.
		MESSAGE("sizeof(DiagnosticArgument)=" << sizeof(DiagnosticArgument));
		MESSAGE("sizeof(TemplateInstantiationFrame)=" << sizeof(TemplateInstantiationFrame));
		MESSAGE("sizeof(DiagnosticNote)=" << sizeof(DiagnosticNote));
		MESSAGE("sizeof(Diagnostic)=" << sizeof(Diagnostic));

		static_assert(sizeof(DiagnosticArgument) <= 32, "argument slot must stay compact");
		static_assert(sizeof(SourceRange) == 24, "range packs two compact locations");
		CHECK(true);
	}
}
