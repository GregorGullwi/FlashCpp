int global_value = 9;

int* staticLocalPtr() {
	static int local_value = 5;
	return &local_value;
}

struct Counter {
	static int member_value;

	static int* memberPtr() {
		return &member_value;
	}
};

int Counter::member_value = 20;

int main() {
	int* global_ptr = &global_value;
	int* local_ptr = staticLocalPtr();
	int* member_ptr = Counter::memberPtr();

	*global_ptr += 1;
	*local_ptr += 4;
	*member_ptr += 3;

	return *global_ptr + *local_ptr + *member_ptr;
}
