struct LocalNestedInner {
	int value;
};

struct LocalNestedOuter {
	LocalNestedInner inner;
};

constexpr int local_object_nested_member_access_result() {
	LocalNestedOuter object{{40}};
	return object.inner.value + object.inner.value;
}

static_assert(local_object_nested_member_access_result() == 80);

int main() {
	return 0;
}