// Regression: class-template partial specializations accept attributes between
// the class-keyword and the specialization name.

template<class T>
struct AttributePartialSpecialization;

template<class T>
struct [[deprecated("partial specialization attribute regression")]]
AttributePartialSpecialization<T*> {
	static constexpr int value = 42;
};

int main() {
	return AttributePartialSpecialization<int*>::value == 42 ? 0 : 1;
}
