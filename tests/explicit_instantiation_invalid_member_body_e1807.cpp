template <typename T>
struct ExplicitBodyCheck {
	int f() {
		typename T::missing_type value;
		return 0;
	}
};

template class ExplicitBodyCheck<int>;

int main() {
	return 0;
}
