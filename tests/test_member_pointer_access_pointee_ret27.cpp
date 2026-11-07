struct Inner {
	int value;
};

struct Outer {
	int direct;
	int* item;
	Inner object;
};

int main() {
	Outer outer{};
	outer.direct = 7;
	int local = 11;
	outer.item = &local;
	outer.object.value = 9;
	int Outer::* direct = &Outer::direct;
	auto item = &Outer::item;
	Inner Outer::* object = &Outer::object;
	if (sizeof(outer.*direct) != sizeof(int)) return 2;
	if (sizeof(outer.*item) != sizeof(int*)) return 3;
	if (sizeof(outer.*object) != sizeof(Inner)) return 4;
	return outer.*direct + *(outer.*item) + (outer.*object).value;
}
