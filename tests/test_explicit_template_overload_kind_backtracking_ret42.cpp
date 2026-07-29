namespace library {

struct Payload {
	int value;
};

template <typename Type>
int select(Type value) {
	return static_cast<int>(sizeof(Type)) + value.value;
}

template <int Value>
int select(Payload value) {
	return Value + value.value;
}

}

int main() {
	library::Payload payload{35};
	return library::select<7>(payload);
}
