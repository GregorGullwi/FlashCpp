template<class T>
concept FourBytes = sizeof(T) == 4;

struct Conversion {
	template<class T> requires FourBytes<T>
	operator T() const {
		return T{};
	}
};

int read(char value) {
	return value;
}

int main() {
	return read(Conversion{});
}
