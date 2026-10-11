template <class T>
struct S {
	T value;
	int operator+(const S* other) const { return static_cast<int>(other->value); }
};

int main() {
	S<int> left{1};
	S<int> right{2};
	return left + &right;
}
