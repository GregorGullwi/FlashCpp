// Test member get() function template for tuple-like structured binding with complex member types:
// struct member (Tag), floating-point member (double), and short integer member.
// Per [dcl.struct.bind]/3: if E has a member named get, use e.get<I>().
// Expected return: 42  (10 + 20 + 12)

namespace std {
	template <typename T>
	struct tuple_size;

	template <unsigned long I, typename T>
	struct tuple_element;
} // namespace std

struct Tag {
	int code;
};

// Forward-declare Record so that std::tuple_element specializations can
// reference it before the complete class definition (needed by the primary
// member template declaration).
struct Record;

namespace std {
	template <> struct tuple_element<0, Record> { using type = Tag; };
	template <> struct tuple_element<1, Record> { using type = double; };
	template <> struct tuple_element<2, Record> { using type = short; };
} // namespace std

struct Record {
	Tag    tag;    // struct member
	double weight; // non-int floating-point member
	short  flags;  // short integral member

	template <unsigned long I>
	typename std::tuple_element<I, Record>::type get() const;
};

template <>
Tag Record::get<0>() const { return tag; }

template <>
double Record::get<1>() const { return weight; }

template <>
short Record::get<2>() const { return flags; }

namespace std {
	template <>
	struct tuple_size<Record> {
		static constexpr unsigned long value = 3;
	};
} // namespace std

int main() {
	Record r;
	r.tag.code = 10;
	r.weight   = 20.0;
	r.flags    = 12;

	auto [t, w, f] = r;
	// t.code == 10, w == 20.0, f == 12  →  10 + 20 + 12 = 42
	return t.code + static_cast<int>(w) + static_cast<int>(f);
}
