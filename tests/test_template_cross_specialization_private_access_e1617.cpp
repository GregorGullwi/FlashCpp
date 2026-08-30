template<class T>
struct PrivateBox {
private:
	int value = 42;

public:
	static int readOther(PrivateBox<double>& other) {
		return other.value;
	}
};

int main() {
	PrivateBox<double> value;
	return PrivateBox<int>::readOther(value);
}
