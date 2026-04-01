using MyInt = int;

struct Box {
	MyInt value;

	operator MyInt() const {
		return value;
	}
};

int main() {
	Box box{42};
	MyInt value = box;
	return value;
}
