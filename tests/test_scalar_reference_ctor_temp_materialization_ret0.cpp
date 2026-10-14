struct LongReferenceCopy {
	long value;
	LongReferenceCopy(const long& argument) noexcept : value(argument) {}
};

struct LongLongReferenceCopy {
	long long value;
	LongLongReferenceCopy(const long long& argument) noexcept : value(argument) {}
};

struct DoubleReferenceCopy {
	double value;
	DoubleReferenceCopy(const double& argument) noexcept : value(argument) {}
};

struct FloatReferenceCopy {
	float value;
	FloatReferenceCopy(const float& argument) noexcept : value(argument) {}
};

struct LongRvalueReferenceCopy {
	long value;
	LongRvalueReferenceCopy(long&& argument) noexcept : value(argument) {}
};

struct MutableLongAlias {
	long* address;
	MutableLongAlias(long& argument) noexcept : address(&argument) {}
};

struct ConstLongAlias {
	const long* address;
	ConstLongAlias(const long& argument) noexcept : address(&argument) {}
};

template<class T> struct TemplatedLongReferenceCopy {
	long value;
	TemplatedLongReferenceCopy(const long& argument) noexcept : value(argument) {}
};

struct TemplatedConstructorReferenceCopy {
	long value;
	template<class T> TemplatedConstructorReferenceCopy(const long& argument, T) noexcept : value(argument) {}
};

struct MixedReferenceAndValue {
	long reference_value;
	int value;
	MixedReferenceAndValue(const long& reference, int copied) noexcept : reference_value(reference), value(copied) {}
};

short makeShort() { return 19; }

int copyFromShortReference(short& source) {
	LongReferenceCopy copy(source);
	return copy.value == source ? 0 : 1;
}

int main() {
	short source = 7;
	LongReferenceCopy as_long(source);
	if (as_long.value != 7) return 1;
	if (copyFromShortReference(source) != 0) return 16;
	LongReferenceCopy from_list{source};
	if (from_list.value != 7) return 13;
	LongReferenceCopy from_constructor_expression = LongReferenceCopy(source);
	if (from_constructor_expression.value != 7) return 14;
	LongLongReferenceCopy as_long_long(source);
	if (as_long_long.value != 7) return 2;
	DoubleReferenceCopy as_double(source);
	if (as_double.value != 7.0) return 3;
	short negative_source = -17;
	LongLongReferenceCopy signed_long_long(negative_source);
	if (signed_long_long.value != -17) return 10;
	DoubleReferenceCopy signed_double(negative_source);
	if (signed_double.value != -17.0) return 11;
	FloatReferenceCopy signed_float(negative_source);
	if (signed_float.value != -17.0f) return 12;
	LongRvalueReferenceCopy as_rvalue_reference(source);
	if (as_rvalue_reference.value != 7) return 4;
	LongReferenceCopy from_prvalue(makeShort());
	if (from_prvalue.value != 19) return 5;
	long mutable_source = 13;
	MutableLongAlias mutable_alias(mutable_source);
	if (mutable_alias.address != &mutable_source) return 6;
	const long const_source = 11;
	ConstLongAlias const_alias(const_source);
	if (const_alias.address != &const_source) return 7;
	TemplatedLongReferenceCopy<int> templated(source);
	if (templated.value != 7) return 8;
	TemplatedConstructorReferenceCopy templated_constructor(source, 1);
	if (templated_constructor.value != 7) return 15;
	MixedReferenceAndValue mixed(source, 23);
	if (mixed.reference_value != 7 || mixed.value != 23) return 9;
	return 0;
}
