# Return-flow analysis

`IrControlFlowGraph` builds a temporary function-local graph from a span of
lowered instructions. `AstToIr::finishFunctionControlFlow` consumes its
`mayReachEnd()` result before appending an implicit return and before emitting
out-of-line function cleanup landing pads. Both ordinary functions and the two
lambda entry points use this policy. Parsing and semantic normalization remain
responsible for legal return operands and concrete return types.

Lowered IR is the analysis boundary: template substitution, discarded
`if constexpr` arms, condition conversions, scope cleanup, switch dispatch,
and goto targets are already resolved. The analysis neither walks source ASTs
nor repeats name lookup or constant evaluation. Scalar literal conversions to
bool retain literal values, so the graph can prune known branch edges without
building a second constant-expression evaluator.

Instruction indices identify graph nodes. A synthetic node at the end of the
span represents normal fallthrough. Ordinary edges, branch alternatives, and
exception edges occupy a fixed three-index successor array per instruction;
there is no separately allocated adjacency vector per node. Labels are lookup
keys, never semantic declaration or type identities. Duplicate/unresolved labels
and malformed region nesting are internal errors. Iterative reachability visits
each reachable node once, including cycles and labels in otherwise dead code.
Storage grows with function size; native stack use does not grow with graph
depth. Large node and worklist arrays use vectors. Inline capacities for the
small lexical region stacks would require measured depth and spill rates.
The node array and reachability bitset are sized once. A preliminary pass counts
labels and maximum lexical region depths to reserve the label map and the loop
and handler stacks exactly. The pending traversal worklist grows on demand;
reserving one slot per instruction would waste space on straight-line functions.

Breakable scopes distinguish switches from iteration statements. A break targets
the innermost breakable scope; a continue searches for the innermost iteration.
The backend consumes the same distinction. It does not infer scope kind from
label spelling or equality.

Resolved calls carry compact control-flow flags. `[[noreturn]]` removes only the
normal successor; a potentially throwing call can still enter a local handler.
`noexcept` removes that exception edge. Indirect calls obtain `noexcept` from
their function signature, while `noreturn` remains a declaration attribute and
does not become a property of function-pointer types. Virtual calls use their
statically selected declaration. A throw, rethrow, or unmatched typed handler
dispatches to the enclosing handler region or escapes the function. A completed
catch transfers to the parent continuation, rather than falling into the next
handler. `Unreachable` is an explicit terminator for the compiler intrinsic;
the x64 backend emits UD2 through its emission helper.

The result is conservative when values or exception types are unknown: both
runtime branch outcomes and both typed-handler match outcomes remain possible.
Operations without a resolved exception contract, such as construction and
destruction, conservatively retain an exceptional edge. This analysis is not a
path-sensitive theorem prover and does not infer termination of arbitrary calls.
An unreachable closing label or trailing cleanup no longer causes a warning.
The Windows SEH extension retains a conservative normal/exception successor for
`SehFinallyCall`: analyzing out-of-line finally funclets requires separate
return/throw summaries, rather than treating their machine-code return as a
return from the enclosing C++ function. That extension limitation is recorded
in `KNOWN_ISSUES.md`; standard C++ try/catch regions are analyzed directly.

The C++20 policy follows [stmt.return](https://timsong-cpp.github.io/cppwp/n4861/stmt.return):
executing non-void fallthrough is undefined behavior, not a reason to reject the
function declaration. A reachable end reports the structured warning
`ControlReachesEndOfValueFunction#1620`. A bare return still confines that
executed UB path to its function and does not supply a language-defined result.
Void functions get an implicit void return; pointers whose base category is
void still require a value. Only the global non-member
[main function](https://timsong-cpp.github.io/cppwp/n4861/basic.start.main) gets an
implicit zero result. Successful compilation renders accumulated warning
records with source locations and diagnostic IDs.
Functions whose synthetic end is unreachable still receive a physical
`Unreachable` terminator. Besides trapping a violated no-return contract, this
keeps the saved return address of a final throwing call inside the function's
unwind range; no source-level return value or normal CFG edge is introduced.

The source regressions exercise real missing paths without executing them, and
check runtime results for valid paths. `tests/run_return_flow_tests.ps1` asserts
the exact warning-name multiset, including warning absence. The native
`tests/architecture/run_return_flow_graph.py` regression checks graph invariants,
exception dispatch, and a 100,000-label/branch chain and cycle under the default
OS stack. Clang's stack-usage output accompanies that run; the analysis adds no
source-language recursion or stack-reserve change.
On Windows clang-cl with `/W4 /WX` and `-fstack-usage`, the graph constructor's
frame is 1,032 bytes and reachability's frame is 312 bytes, both static.
The touched attribute-parser translation unit also compiles warning-clean with
those flags. Comparing the original source against the updated source using
the same current headers gives an unchanged 168-byte `parse_attributes` frame
and an unchanged 200-byte `skip_cpp_attributes_with_info` frame. The iterative
`consume_cpp_attribute_blocks` helper grows from 424 to 1,432 bytes because of
structured invalid-attribute diagnostics; it adds no recursive call or depth.
