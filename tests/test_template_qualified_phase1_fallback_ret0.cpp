// Regression: qualified non-dependent call inside a template should not fail
// when declarations are visible at instantiation time but were filtered by the
// phase-1 cutoff.

namespace ns {
int late_compare(int value);

template <typename T>
int call_late() {
	return ns::late_compare(5);
}

int late_compare(int value) {
	return value + 2;
}
} // namespace ns

int main() {
	return ns::call_late<int>() == 7 ? 0 : 1;
}
