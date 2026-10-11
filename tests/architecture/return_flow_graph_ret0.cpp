#include <cstdio>
#include <string>

#include "IRControlFlow.h"

ChunkedStringAllocator gChunkedStringAllocator(16 * 1024 * 1024);

namespace {

StringHandle label(std::string_view name) {
	return StringTable::getOrInternStringHandle(name);
}

template <typename Payload>
IrInstruction instruction(IrOpcode opcode, Payload payload) {
	return IrInstruction(opcode, std::move(payload), Token{});
}

IrInstruction plainInstruction(IrOpcode opcode) {
	return IrInstruction(opcode, std::vector<IrOperand>{}, Token{});
}

IrInstruction labelInstruction(std::string_view name) {
	return instruction(IrOpcode::Label, LabelOp{label(name)});
}

IrInstruction branchInstruction(std::string_view name) {
	return instruction(IrOpcode::Branch, BranchOp{label(name)});
}

IrInstruction callInstruction(uint8_t flags) {
	CallOp call{};
	call.control_flow_flags = flags;
	return instruction(IrOpcode::FunctionCall, std::move(call));
}

int expect(bool condition, int code) {
	if (!condition) {
		std::fprintf(stderr, "return-flow graph regression failed at check %d\n", code);
		return code;
	}
	return 0;
}

bool throwsInternalError(const std::vector<IrInstruction>& instructions) {
	try {
		IrControlFlowGraph graph(instructions);
		(void)graph;
		return false;
	} catch (const InternalError&) {
		return true;
	}
}

int checkEndAndReturns() {
	{
		IrControlFlowGraph graph(std::span<const IrInstruction>{});
		if (const int error = expect(graph.mayReachEnd(), 1)) return error;
	}
	{
		std::vector<IrInstruction> code{plainInstruction(IrOpcode::Return)};
		IrControlFlowGraph graph(code);
		if (const int error = expect(!graph.mayReachEnd(), 2)) return error;
	}
	{
		std::vector<IrInstruction> code;
		code.push_back(instruction(IrOpcode::ConditionalBranch, CondBranchOp{
			.label_true = label("both_return_true"), .label_false = label("both_return_false"),
			.condition = TypedValue{.size_in_bits = SizeInBits{}, .value = TempVar{1}}}));
		code.push_back(labelInstruction("both_return_true"));
		code.push_back(plainInstruction(IrOpcode::Return));
		code.push_back(labelInstruction("both_return_false"));
		code.push_back(plainInstruction(IrOpcode::Return));
		IrControlFlowGraph graph(code);
		if (const int error = expect(!graph.mayReachEnd(), 3)) return error;
	}
	{
		std::vector<IrInstruction> code{plainInstruction(IrOpcode::Return), labelInstruction("dead_tail")};
		IrControlFlowGraph graph(code);
		if (const int error = expect(!graph.mayReachEnd(), 4)) return error;
	}
	return 0;
}

int checkCallEdges() {
	const uint8_t no_return = static_cast<uint8_t>(IrCallFlowFlags::NoNormalReturn);
	const uint8_t no_throw = static_cast<uint8_t>(IrCallFlowFlags::NoThrow);
	{
		std::vector<IrInstruction> code{callInstruction(no_return | IrCallFlowFlags::NoThrow)};
		IrControlFlowGraph graph(code);
		if (const int error = expect(!graph.mayReachEnd(), 5)) return error;
	}
	{
		std::vector<IrInstruction> code{callInstruction(no_throw), plainInstruction(IrOpcode::Return)};
		IrControlFlowGraph graph(code);
		if (const int error = expect(!graph.mayReachEnd(), 6)) return error;
	}
	{
		std::vector<IrInstruction> code;
		code.push_back(instruction(IrOpcode::TryBegin, BranchOp{label("call_handler")}));
		code.push_back(callInstruction(static_cast<uint8_t>(IrCallFlowFlags::NoNormalReturn)));
		code.push_back(plainInstruction(IrOpcode::TryEnd));
		code.push_back(labelInstruction("call_handler"));
		IrControlFlowGraph graph(code);
		if (const int error = expect(graph.mayReachEnd(), 7)) return error;
	}
	{
		std::vector<IrInstruction> code;
		code.push_back(instruction(IrOpcode::TryBegin, BranchOp{label("no_throw_handler")}));
		code.push_back(callInstruction(no_throw));
		code.push_back(plainInstruction(IrOpcode::TryEnd));
		code.push_back(plainInstruction(IrOpcode::Return));
		code.push_back(labelInstruction("no_throw_handler"));
		IrControlFlowGraph graph(code);
		if (const int error = expect(!graph.mayReachEnd(), 8)) return error;
	}
	return 0;
}

int checkNestedCatchMismatch() {
	std::vector<IrInstruction> code;
	code.push_back(instruction(IrOpcode::TryBegin, BranchOp{label("outer_handler")}));
	code.push_back(instruction(IrOpcode::TryBegin, BranchOp{label("inner_handler")}));
	code.push_back(callInstruction(static_cast<uint8_t>(IrCallFlowFlags::NoNormalReturn)));
	code.push_back(plainInstruction(IrOpcode::TryEnd));
	code.push_back(plainInstruction(IrOpcode::TryEnd));
	code.push_back(branchInstruction("normal_done"));
	code.push_back(labelInstruction("inner_handler"));
	CatchBeginOp typed_catch{};
	typed_catch.catch_end_label = "next_inner_catch";
	code.push_back(instruction(IrOpcode::CatchBegin, typed_catch));
	code.push_back(plainInstruction(IrOpcode::Unreachable));
	code.push_back(labelInstruction("next_inner_catch"));
	CatchBeginOp catch_all{};
	catch_all.is_catch_all = true;
	code.push_back(instruction(IrOpcode::CatchBegin, catch_all));
	code.push_back(instruction(IrOpcode::CatchEnd, CatchEndOp{"mismatch_done"}));
	code.push_back(labelInstruction("normal_done"));
	code.push_back(plainInstruction(IrOpcode::Return));
	code.push_back(labelInstruction("outer_handler"));
	code.push_back(plainInstruction(IrOpcode::Return));
	code.push_back(labelInstruction("mismatch_done"));
	IrControlFlowGraph graph(code);
	return expect(graph.mayReachEnd(), 9);
}

int checkLoopContexts() {
	std::vector<IrInstruction> code;
	code.push_back(instruction(IrOpcode::LoopBegin, LoopBeginOp{
		.loop_start_label = label("outer_start"), .loop_end_label = label("outer_end"),
		.loop_increment_label = label("outer_increment")}));
	code.push_back(labelInstruction("outer_start"));
	code.push_back(instruction(IrOpcode::LoopBegin, LoopBeginOp{
		.loop_start_label = label("switch_start"), .loop_end_label = label("switch_end"),
		.loop_increment_label = label("switch_end"), .kind = LoopConstructKind::Switch}));
	code.push_back(labelInstruction("switch_start"));
	code.push_back(plainInstruction(IrOpcode::Break));
	code.push_back(labelInstruction("switch_end"));
	code.push_back(plainInstruction(IrOpcode::LoopEnd));
	code.push_back(plainInstruction(IrOpcode::Continue));
	code.push_back(labelInstruction("outer_increment"));
	code.push_back(plainInstruction(IrOpcode::Break));
	code.push_back(labelInstruction("outer_end"));
	code.push_back(plainInstruction(IrOpcode::LoopEnd));
	code.push_back(plainInstruction(IrOpcode::Return));
	IrControlFlowGraph graph(code);
	return expect(!graph.mayReachEnd(), 10);
}

int checkInvalidLabels() {
	if (const int error = expect(throwsInternalError({branchInstruction("missing_label")}), 11)) return error;
	std::vector<IrInstruction> duplicate{labelInstruction("duplicate"), labelInstruction("duplicate")};
	return expect(throwsInternalError(duplicate), 12);
}

int checkDeepFlatGraph() {
	constexpr size_t count = 100000;
	std::vector<IrInstruction> code;
	code.reserve(count * 2);
	for (size_t i = 0; i < count; ++i) {
		const std::string name = "flat_cfg_" + std::to_string(i);
		code.push_back(branchInstruction(name));
		code.push_back(labelInstruction(name));
	}
	IrControlFlowGraph graph(code);
	if (const int error = expect(graph.mayReachEnd(), 13)) return error;
	code[code.size() - 2] = branchInstruction("flat_cfg_0");
	IrControlFlowGraph cycle(code);
	return expect(!cycle.mayReachEnd(), 14);
}

}

int main() {
	for (auto check : {checkEndAndReturns, checkCallEdges, checkNestedCatchMismatch, checkLoopContexts, checkInvalidLabels, checkDeepFlatGraph}) {
		if (const int error = check()) return error;
	}
	return 0;
}
