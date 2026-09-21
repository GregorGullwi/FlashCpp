// Dropping the const pointee of an ordered pointer while converting to void*
// is not a valid [conv.ptr] conversion; only const void* is reachable.
int (* const (*value)[3])[4] = nullptr;

int consume(void*);

int main() {
	consume(value);
	return 0;
}
