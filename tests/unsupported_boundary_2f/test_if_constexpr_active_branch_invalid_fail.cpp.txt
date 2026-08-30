template <class T>
int select_active_invalid_branch() {
	if constexpr (true) {
		T value{};
		return value.operator->();
	} else {
		return 0;
	}
}

int main() {
	return select_active_invalid_branch<int>();
}
