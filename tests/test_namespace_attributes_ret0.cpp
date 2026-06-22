// Regression: namespace attributes are permitted between the namespace keyword
// and the namespace name.

#define ATTR_NAMESPACE [[deprecated("namespace attribute regression")]]

namespace ATTR_NAMESPACE attr_ns {
	int value() {
		return 42;
	}
}

int main() {
	return attr_ns::value() == 42 ? 0 : 1;
}
