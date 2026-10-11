#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "IRTypes.h"

// A function-local graph over lowered instructions. The synthetic node at size()
// represents normal fallthrough; escaping exceptions have no successor. Keeping
// instruction indices as identity also permits labels inside otherwise dead code.
// No AST walking, parsing, emission, or diagnostic policy belongs in this analysis.
class IrControlFlowGraph {
public:
	explicit IrControlFlowGraph(std::span<const IrInstruction> instructions) : successors_(instructions.size()) {
		size_t label_count = 0;
		size_t loop_depth = 0;
		size_t max_loop_depth = 0;
		size_t handler_depth = 0;
		size_t max_handler_depth = 0;
		for (const auto& instruction : instructions) {
			switch (instruction.getOpcode()) {
			case IrOpcode::Label:
				++label_count;
				break;
			case IrOpcode::LoopBegin:
				max_loop_depth = std::max(max_loop_depth, ++loop_depth);
				break;
			case IrOpcode::LoopEnd:
				if (loop_depth == 0) {
					throw InternalError("Unbalanced loop in function control-flow graph");
				}
				--loop_depth;
				break;
			case IrOpcode::TryBegin:
			case IrOpcode::SehTryBegin:
				max_handler_depth = std::max(max_handler_depth, ++handler_depth);
				break;
			case IrOpcode::TryEnd:
			case IrOpcode::SehTryEnd:
				if (handler_depth == 0) {
					throw InternalError("Unbalanced try region in function control-flow graph");
				}
				--handler_depth;
				break;
			default:
				break;
			}
		}
		if (loop_depth != 0 || handler_depth != 0) {
			throw InternalError("Unclosed region in function control-flow graph");
		}
		std::unordered_map<StringHandle, size_t> labels;
		labels.reserve(label_count);
		for (size_t i = 0; i < instructions.size(); ++i) {
			if (instructions[i].getOpcode() == IrOpcode::Label) {
				if (!labels.emplace(instructions[i].getTypedPayload<LabelOp>().label_name, i).second) {
					throw InternalError("Duplicate label in function control-flow graph");
				}
			}
		}
		auto target = [&](StringHandle label) -> size_t {
			auto it = labels.find(label);
			if (it == labels.end()) {
				throw InternalError("Unresolved label in function control-flow graph");
			}
			return it->second;
		};
		auto textTarget = [&](std::string_view label) -> size_t { return target(StringTable::getOrInternStringHandle(label)); };
		std::vector<const LoopBeginOp*> loops;
		loops.reserve(max_loop_depth);
		std::vector<size_t> handlers;
		handlers.reserve(max_handler_depth);
		for (size_t i = 0; i < instructions.size(); ++i) {
			const auto& instruction = instructions[i];
			auto& edges = successors_[i];
			edges = {i + 1, noSuccessor, noSuccessor};
			const size_t exception_target = handlers.empty() ? noSuccessor : handlers.back();
			switch (instruction.getOpcode()) {
			case IrOpcode::FunctionDecl:
				throw InternalError("Nested function in function control-flow graph");
			case IrOpcode::Branch:
				edges[0] = target(instruction.getTypedPayload<BranchOp>().target_label);
				break;
			case IrOpcode::ConditionalBranch: {
				const auto& branch = instruction.getTypedPayload<CondBranchOp>();
				const auto truth = constantTruth(branch.condition.value);
				edges[0] = !truth.has_value() || *truth ? target(branch.label_true) : noSuccessor;
				edges[1] = !truth.has_value() || !*truth ? target(branch.label_false) : noSuccessor;
				break;
			}
			case IrOpcode::LoopBegin:
				loops.push_back(&instruction.getTypedPayload<LoopBeginOp>());
				break;
			case IrOpcode::LoopEnd:
				if (loops.empty()) {
					throw InternalError("Unbalanced loop in function control-flow graph");
				}
				loops.pop_back();
				break;
			case IrOpcode::Break:
			case IrOpcode::Continue:
				if (loops.empty()) {
					throw InternalError("Loop transfer outside loop in function control-flow graph");
				}
				if (instruction.getOpcode() == IrOpcode::Break) {
					edges[0] = target(loops.back()->loop_end_label);
				} else {
					auto loop = std::find_if(loops.rbegin(), loops.rend(), [](const LoopBeginOp* context) {
						return context->kind == LoopConstructKind::Iteration;
					});
					if (loop == loops.rend()) {
						throw InternalError("Continue outside iteration in function control-flow graph");
					}
					edges[0] = target((*loop)->loop_increment_label);
				}
				break;
			case IrOpcode::TryBegin:
			case IrOpcode::SehTryBegin:
				handlers.push_back(target(instruction.getTypedPayload<BranchOp>().target_label));
				break;
			case IrOpcode::TryEnd:
			case IrOpcode::SehTryEnd:
				if (handlers.empty()) {
					throw InternalError("Unbalanced try region in function control-flow graph");
				}
				handlers.pop_back();
				break;
			case IrOpcode::CatchBegin: {
				const auto& handler = instruction.getTypedPayload<CatchBeginOp>();
				if (!handler.is_catch_all) {
					edges[1] = textTarget(handler.catch_end_label);
				}
				break;
			}
			case IrOpcode::CatchEnd:
				edges[0] = textTarget(instruction.getTypedPayload<CatchEndOp>().continuation_label);
				break;
			case IrOpcode::Throw:
			case IrOpcode::Rethrow:
			case IrOpcode::ElfCatchNoMatch:
				edges[0] = exception_target;
				break;
			case IrOpcode::Return:
			case IrOpcode::Unreachable:
			case IrOpcode::FunctionCleanupLP:
			case IrOpcode::SehFinallyEnd:
			case IrOpcode::SehFilterEnd:
				edges[0] = noSuccessor;
				break;
			case IrOpcode::FunctionCall:
				setCallSuccessors(edges, instruction.getTypedPayload<CallOp>(), exception_target);
				break;
			case IrOpcode::IndirectCall:
				setCallSuccessors(edges, instruction.getTypedPayload<IndirectCallOp>(), exception_target);
				break;
			case IrOpcode::VirtualCall:
				setCallSuccessors(edges, instruction.getTypedPayload<VirtualCallOp>(), exception_target);
				break;
			case IrOpcode::ConstructorCall:
			case IrOpcode::DestructorCall:
			case IrOpcode::HeapAlloc:
			case IrOpcode::HeapAllocArray:
			case IrOpcode::HeapFree:
			case IrOpcode::HeapFreeArray:
			case IrOpcode::DynamicCast:
			case IrOpcode::Typeid:
				edges[2] = exception_target;
				break;
			case IrOpcode::SehLeave:
				edges[0] = textTarget(instruction.getTypedPayload<SehLeaveOp>().target_label);
				break;
			case IrOpcode::SehFinallyCall:
				// SEH funclets are a target extension. Until they have return/throw
				// summaries, conservatively retain the normal continuation.
				edges[0] = textTarget(instruction.getTypedPayload<SehFinallyCallOp>().end_label);
				edges[2] = exception_target;
				break;
			default:
				break;
			}
		}
		if (!loops.empty() || !handlers.empty()) {
			throw InternalError("Unclosed region in function control-flow graph");
		}
	}

	bool mayReachEnd() const {
		std::vector<bool> reached(successors_.size() + 1);
		std::vector<size_t> worklist{0};
		reached[0] = true;
		while (!worklist.empty()) {
			const size_t current = worklist.back();
			worklist.pop_back();
			if (current == successors_.size()) {
				return true;
			}
			for (size_t next : successors_[current]) {
				if (next != noSuccessor && !reached[next]) {
					reached[next] = true;
					worklist.push_back(next);
				}
			}
		}
		return false;
	}

private:
	template <typename Call>
	static void setCallSuccessors(std::array<size_t, 3>& edges, const Call& call, size_t exception_target) {
		if (!call.hasNormalReturn()) {
			edges[0] = noSuccessor;
		}
		if (call.mayThrow()) {
			edges[2] = exception_target;
		}
	}

	static std::optional<bool> constantTruth(const IrValue& value) {
		if (const auto* integer = std::get_if<unsigned long long>(&value)) {
			return *integer != 0;
		}
		if (const auto* floating = std::get_if<double>(&value)) {
			return *floating != 0.0;
		}
		return std::nullopt;
	}

	static constexpr size_t noSuccessor = std::numeric_limits<size_t>::max();
	std::vector<std::array<size_t, 3>> successors_;
};
