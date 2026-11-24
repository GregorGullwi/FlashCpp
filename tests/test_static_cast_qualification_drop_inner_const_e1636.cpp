// C++20 [expr.static.cast]/5 permits only an implicit conversion sequence, so
// removing the const qualification of an inner pointer level is not a valid
// static_cast. [conv.qual]/3 keeps as many cv-qualifiers as either type has, so
// "long long* const*" to "long long**" cannot be a qualification conversion.

int main() {
	long long value = 7;
	long long* pointer = &value;
	long long* const* inner_const = &pointer;
	long long** plain = static_cast<long long**>(inner_const);
	(void)plain;
	return 0;
}
