struct Box {
	int value() const {
		return 42;
	}
};

using Alias = Box;

int main() {
	Alias box;
	Alias* ptr = &box;
	return ptr->value() == 42 ? 0 : 1;
}
