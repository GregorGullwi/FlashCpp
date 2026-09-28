enum FixedByte : unsigned char { fixed_byte_value = 1 };
FixedByte fixed_byte_lvalue = fixed_byte_value;

struct UnderlyingReferenceChoice {
	char bytes[1];
};

struct IntChoice {
	char bytes[2];
};

struct MutableReferenceChoice {
	char bytes[3];
};

UnderlyingReferenceChoice choose_const_reference(const unsigned char&);
IntChoice choose_const_reference(int);

UnderlyingReferenceChoice choose_rvalue_reference(unsigned char&&);
IntChoice choose_rvalue_reference(int);

MutableReferenceChoice choose_mutable_reference(unsigned char&);
IntChoice choose_mutable_reference(int);

static_assert(sizeof(decltype(choose_const_reference(fixed_byte_lvalue))) ==
	sizeof(UnderlyingReferenceChoice));
static_assert(sizeof(decltype(choose_rvalue_reference(fixed_byte_lvalue))) ==
	sizeof(UnderlyingReferenceChoice));
static_assert(sizeof(decltype(choose_mutable_reference(fixed_byte_lvalue))) ==
	sizeof(IntChoice));

int main() {
	return 0;
}
