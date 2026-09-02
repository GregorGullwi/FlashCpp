extern int inherited_values[2];
int inherited_values[] = {17};

extern int inherited_without_initializer[2];
int inherited_without_initializer[];

int main() {
	return sizeof(inherited_values) == 8 &&
		inherited_values[0] == 17 && inherited_values[1] == 0 &&
		sizeof(inherited_without_initializer) == 8 &&
		inherited_without_initializer[0] == 0 &&
		inherited_without_initializer[1] == 0 ? 42 : 1;
}
