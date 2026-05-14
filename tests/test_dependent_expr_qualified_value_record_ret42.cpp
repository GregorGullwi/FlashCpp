// Regression: expression qualified-ids whose owner is a dependent type
// parameter keep semantic owner/member information through substitution.

template <typename T>
int read_value() {
	return T::value;
}

struct Answer {
	static constexpr int value = 42;
};

int main() {
	return read_value<Answer>();
}
