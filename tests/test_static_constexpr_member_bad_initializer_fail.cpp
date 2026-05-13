struct Broken {
	static constexpr int value = MissingValue;
};

int main() {
	return Broken::value;
}
