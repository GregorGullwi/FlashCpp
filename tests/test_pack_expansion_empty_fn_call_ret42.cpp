// Regression test: empty function-argument pack expansions must erase the
// PackExpansionExprNode before semantic analysis.

int answer() {
	return 42;
}

template<typename... Args>
int forwardAnswer(Args... args) {
	return answer(args...);
}

int main() {
	return forwardAnswer();
}
