// Regression test: standalone 0 literals in preprocessor expressions must
// still parse after the string_view-based evaluator refactor.

#if 0
int inactiveBranch() { return 1; }
#else
int inactiveBranch() { return 0; }
#endif

int main() {
#if 0
	return 1;
#elif 0
	return 2;
#else
	return inactiveBranch();
#endif
}
