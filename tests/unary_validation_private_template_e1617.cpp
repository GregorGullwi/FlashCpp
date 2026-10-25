class Value {
	int operator-() const { return 7; }
};
template<class T> int read(T value) { return -value; }
int main() { return read(Value{}); }
