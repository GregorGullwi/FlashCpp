// Test: out-of-line nested class definition in template
// Verifies parser handles template<T> class Outer<T>::Inner { ~Inner() ... }
template<typename T>
struct Outer {
	class Inner;
	T value;
};

template<typename T>
class Outer<T>::Inner {
	T data;
public:
	Inner(T d) : data(d) {}
	~Inner() {}
	T get() { return data; }
};

int main() {
	Outer<int> o;
	o.value = 42;
	return o.value;
}
