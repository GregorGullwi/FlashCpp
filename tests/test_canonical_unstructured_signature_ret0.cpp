// Flat TypeIndex function-signature projections (return pointer depth +
// parameter categories without FunctionCallableTypes return storage) must
// import as Supported callables, including when mixed with a struct typedef.
struct Box {
	int value;
};

using FlatCallback = int (*)(short, Box*);

int invoke(FlatCallback callback, short tag, Box* box) {
	return callback(tag, box);
}

int read_box(short tag, Box* box) {
	return static_cast<int>(tag) + box->value;
}

int main() {
	Box box{5};
	return invoke(read_box, 3, &box) - 8;
}
