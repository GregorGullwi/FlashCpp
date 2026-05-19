struct Box {
	using Value = int;
	using Pointer = int*;

	int set(Value value);
	Value get();
	int read(Pointer value);
};

int Box::set(int value) {
	return value;
}

int Box::get() {
	return 7;
}

int Box::read(int* value) {
	return *value;
}

struct Payload {
	int value;
};

struct UsesPayload {
	using Alias = Payload;

	int extract(Alias payload);
};

int UsesPayload::extract(Payload payload) {
	return payload.value;
}

int main() {
	Box box;
	int value = 5;
	if (box.set(42) != 42) {
		return 1;
	}
	if (box.get() != 7) {
		return 2;
	}
	if (box.read(&value) != 5) {
		return 3;
	}
	UsesPayload uses_payload;
	return uses_payload.extract(Payload{9}) == 9 ? 0 : 4;
}
