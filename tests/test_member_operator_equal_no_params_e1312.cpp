// Diagnostic regression for ordinary operator arity.
struct Value {
	bool operator==() const {
		return true;
	}
};

int main() {
	return 0;
}
