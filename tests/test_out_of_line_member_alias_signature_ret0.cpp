struct Box {
	using Value = int;
	using Pointer = int*;

	int set(Value value);
	Value get();
	int get_reverse();
	int read(Pointer value);
	int read_reverse(int* value);
};

int Box::set(int value) {
	return value;
}

int Box::get() {
	return 7;
}

Box::Value Box::get_reverse() {
	return 8;
}

int Box::read(int* value) {
	return *value;
}

int Box::read_reverse(Box::Pointer value) {
	return *value + 1;
}

struct Payload {
	int value;
};

struct UsesPayload {
	using Alias = Payload;

	int extract(Alias payload);
	int extract_reverse(Payload payload);
	Alias make();
};

int UsesPayload::extract(Payload payload) {
	return payload.value;
}

int UsesPayload::extract_reverse(UsesPayload::Alias payload) {
	return payload.value + 1;
}

UsesPayload::Alias UsesPayload::make() {
	return Payload{11};
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
	if (box.get_reverse() != 8) {
		return 4;
	}
	if (box.read_reverse(&value) != 6) {
		return 5;
	}
	UsesPayload uses_payload;
	if (uses_payload.extract(Payload{9}) != 9) {
		return 6;
	}
	if (uses_payload.extract_reverse(Payload{9}) != 10) {
		return 7;
	}
	return uses_payload.make().value == 11 ? 0 : 8;
}
