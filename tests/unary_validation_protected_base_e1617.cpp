struct Base {
protected:
	int operator+() { return 3; }
};
struct Value : Base {
	int read(Base& base) { return +base; }
};
