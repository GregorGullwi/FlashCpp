struct Payload { short value; char tag; };
template<class T> struct Holder { T value; };
int add_two(int value) { return value + 2; }
int subtract_two(int value) { return value - 2; }
Holder<long long> make_box(int value) { return Holder<long long>{value}; }

int main() {
	short rows[2][3] = {{1, 2, 3}, {4, 5, 6}};
	short (*const selected)[3] = rows[0][0] ? rows : &rows[1];
	if (selected[1][2] != 6 || sizeof(*selected) != 3 * sizeof(short)) { return 1; }
	Payload records[2] = {{40, 3}, {42, 7}};
	Payload* record = records[0].value ? records : &records[1];
	if (record[1].value != 42 || record[1].tag != 7) { return 2; }
	Holder<long long> boxes[2] = {{40}, {42}};
	Holder<long long>* box = boxes[0].value ? boxes : &boxes[1];
	if (box[1].value != 42) { return 3; }
	int (*callbacks[2])(int) = {&add_two, &subtract_two};
	int (**callback)(int) = rows[0][0] ? callbacks : &callbacks[1];
	using Callback = int (*)(int);
	Callback const& reference = callbacks[0];
	using BoxCallback = Holder<long long> (*)(int);
	BoxCallback factory = &make_box;
	BoxCallback const& factory_reference = factory;
	Holder<long long> made = factory_reference(42);
	return callback[0](40) == 42 && callback[1](44) == 42 && reference(40) == 42 && made.value == 42 ? 0 : 4;
}
