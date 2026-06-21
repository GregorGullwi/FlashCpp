// Regression: cast type-ids must accept postfix cv-qualifiers on typedef names
// before pointer declarators.

typedef int Scalar;

int main() {
	int value = 7;
	Scalar const* c_style = (Scalar const*)&value;
	Scalar const* cpp_style = static_cast<Scalar const*>(&value);
	return (*c_style == 7 && *cpp_style == 7) ? 0 : 1;
}
