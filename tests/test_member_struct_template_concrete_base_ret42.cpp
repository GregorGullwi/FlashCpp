struct MyBase {
	int get_val() { return 42; }
};

// Primary template member struct with concrete base class
class Outer {
public:
	template <typename T>
	struct Derived : MyBase {
		T val;
	};
};

int main() {
	Outer::Derived<int> d;
	return d.get_val();
}
