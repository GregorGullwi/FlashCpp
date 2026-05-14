int gx = 7;
int gy = 7;

template <auto& Ref>
struct auto_ref_tag {
	static constexpr int value = 1;
};

template <>
struct auto_ref_tag<gx> {
	static constexpr int value = 10;
};

template <>
struct auto_ref_tag<gy> {
	static constexpr int value = 20;
};

int main() {
	if (auto_ref_tag<gx>::value != 10) return 1;
	if (auto_ref_tag<gy>::value != 20) return 2;
	return 0;
}
