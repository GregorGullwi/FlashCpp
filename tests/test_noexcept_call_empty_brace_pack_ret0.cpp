// Regression: noexcept operand with Type{} empty braced init as call argument.

constexpr bool helper(int) { return true; }

template <class Indices>
constexpr bool probe() noexcept(noexcept(helper(Indices{}))) {
	return helper(Indices{});
}

int main() {
	return probe<int>() ? 0 : 1;
}
