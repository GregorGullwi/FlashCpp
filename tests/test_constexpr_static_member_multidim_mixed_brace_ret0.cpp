struct GridHolder {
	static constexpr int arr[2][3] = {1, 2, 3, {4, 5, 6}};
};

static_assert(GridHolder::arr[0][0] == 1);
static_assert(GridHolder::arr[0][1] == 2);
static_assert(GridHolder::arr[0][2] == 3);
static_assert(GridHolder::arr[1][0] == 4);
static_assert(GridHolder::arr[1][1] == 5);
static_assert(GridHolder::arr[1][2] == 6);

int main() {
	return 0;
}
