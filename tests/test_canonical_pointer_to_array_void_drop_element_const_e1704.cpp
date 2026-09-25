const int (*value)[3] = nullptr;
int consume(void* pointer);

int main() {
	return consume(value);
}
