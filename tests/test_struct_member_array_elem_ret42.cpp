struct Item {
	int value;
};

struct Container {
	Item items[3];
};

int main() {
	Container c;
	c.items[0].value = 10;
	c.items[1].value = 20;
	c.items[2].value = 12;
	return c.items[0].value + c.items[1].value + c.items[2].value;  // 42
}
