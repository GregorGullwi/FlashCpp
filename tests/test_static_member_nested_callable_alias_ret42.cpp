using Transform = int(int);
using TransformPointer = Transform*;

struct NonTemplateStaticTransform {
	static inline TransformPointer (*operations)[2] = nullptr;
};

template <typename T>
struct StaticTransform {
	static inline TransformPointer (*operations)[2] = nullptr;
};

template <>
struct StaticTransform<int> {
	static inline TransformPointer (*operations)[2] = nullptr;
};

int main() {
	return NonTemplateStaticTransform::operations == nullptr &&
		StaticTransform<char>::operations == nullptr &&
		StaticTransform<int>::operations == nullptr &&
		sizeof(StaticTransform<char>::operations) == sizeof(void*) &&
		sizeof(StaticTransform<int>::operations) == sizeof(void*)
		? 42
		: 0;
}
