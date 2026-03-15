// Regression test: ExpressionSubstitutor must preserve ExpressionNode wrapping
// for unhandled ExpressionNode variant types (e.g. NoexceptExprNode).
//
// When ExpressionSubstitutor::substitute() encounters an ExpressionNode variant
// type that has no dedicated handler (like NoexceptExprNode), the std::visit
// path must re-wrap the original node in ExpressionNode before returning.
// Downstream callers (e.g. Parser_Templates_Inst_ClassTemplate.cpp) check
// is<ExpressionNode>() on the substituted result; if wrapping is lost they
// silently skip evaluation and the non-type default gets the wrong value.
//
// noexcept(true) is always true regardless of template parameter, so
// Helper<T>::value should be 1 after substituting T = int through Wrap<int>.

template<typename T, bool B = noexcept(true)>
struct Helper {
	static const bool value = B;
};

template<typename T>
struct Wrap : Helper<T> {};

int main() {
	// noexcept(true) == true, so Wrap<int>::value must be 1
	return Wrap<int>::value - 1;  // 0 on success
}
