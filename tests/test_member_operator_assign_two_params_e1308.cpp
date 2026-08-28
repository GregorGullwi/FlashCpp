// Diagnostic regression for assignment operator arity.
struct Value {
	Value& operator=(const Value& other, int extra) {
		return *this;
	}
};

int main() {
	return 0;
}
