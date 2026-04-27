struct Item {
	int value;
	short bonus;
};

struct ItemPtr {
	Item data[2];

	operator Item*() {
		return data;
	}
};

int main() {
	ItemPtr items;
	items.data[0].value = 35;
	items.data[1].bonus = 7;

	return items[0].value + 1[items].bonus;
}
