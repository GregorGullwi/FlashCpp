struct Base {
protected:
	int value = 42;
};

struct Derived : Base {
	int read(Base& object) {
		return object.value;
	}
};

int main() {
	Derived object;
	return object.read(object);
}
