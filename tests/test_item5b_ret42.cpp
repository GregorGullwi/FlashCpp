// Test: member struct template base class - verify base class is instantiated
// when the partial specialization is instantiated

struct IntBase {
	int get_value() { return 42; }
};

class Outer {
public:
	template <typename T>
	struct Wrapper : IntBase {
		T data;
	};
};

int main() {
	Outer::Wrapper<int> w;
	w.data = 0;
	return w.get_value();  // inherited from IntBase
}
