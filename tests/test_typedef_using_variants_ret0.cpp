// Hardened test: typedef and using with enums, structs, namespaces — simple + template + nested

// 1. Non-template: typedef enum inside struct, accessed via qualified names
struct Plain {
	typedef enum { A = 10,
				   B = 20 } Tag;
};

// 2. Non-template: using alias for enum inside struct
struct WithUsing {
	enum class Color { Red = 1,
					   Green = 2 };
	using ColorAlias = Color;
};

// 3. Non-template: chained typedefs inside struct
struct Chained {
	typedef int MyInt;
	typedef MyInt AliasInt;	// uses MyInt from same struct body
};

// 4. Template: typedef enum inside template struct
template <typename T>
struct TBox {
	typedef enum { Lo = 1,
				   Hi = 2 } Range;
	T val;
};

// 5. Template: using alias inside template struct referencing itself
template <typename T>
struct Wrap {
	using value_type = T;
	value_type data;	 // uses value_type defined just above
};

// 6. Namespace + struct + typedef enum
namespace ns2 {
struct NS2Container {
	typedef enum { Off = 0,
				   On = 1 } Switch;
};
} // namespace ns2

int main() {
	// 1
	if (Plain::A != 10)
		return 1;
	if (Plain::B != 20)
		return 2;

	// 2
	if ((int)WithUsing::Color::Red != 1)
		return 3;

	// 3
	Chained::AliasInt x = 42;
	if (x != 42)
		return 4;

	// 4
	TBox<int>::Range r = TBox<int>::Lo;
	if ((int)r != 1)
		return 5;
	if (TBox<int>::Hi != 2)
		return 6;

	// 5
	Wrap<int> w;
	w.data = 99;
	if (w.data != 99)
		return 7;

	// 6
	ns2::NS2Container::Switch s = ns2::NS2Container::On;
	if ((int)s != 1)
		return 8;
	if (ns2::NS2Container::Off != 0)
		return 9;

	return 0;
}
