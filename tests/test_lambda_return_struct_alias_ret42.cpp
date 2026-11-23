// An alias denotes the same type as its underlying struct, so a lambda can
// return both spellings. The compare must decide by canonical identity rather
// than by the alias spelling or by an unshared index.
struct Record {
	int value;
};

using RecordAlias = Record;

Record first{7};
RecordAlias second{9};

int use_alias(int flag) {
	auto select = [&]() {
		if (flag) {
			return first;
		}
		return second;
	};
	return select().value;
}

int main() {
	if (use_alias(1) != 7) {
		return 1;
	}
	if (use_alias(0) != 9) {
		return 2;
	}
	return 42;
}
