// Regression: a '<' after a dependent expression qualified-id is a relational
// operator here, not a template-argument-list for the member name.

template <typename T>
int classify() {
	return T::value < 10 ? 42 : 7;
}

struct Small {
	static constexpr int value = 3;
};

int main() {
	return classify<Small>();
}
