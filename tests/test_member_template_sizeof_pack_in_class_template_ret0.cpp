template <typename... Ts>
struct Holder {
	template <typename... Us>
	int combined_pack_size(Us...) const {
		return static_cast<int>(sizeof...(Ts)) * 10
			 + static_cast<int>(sizeof...(Us));
	}
};

int main() {
	Holder<int, char> holder;
	return holder.combined_pack_size(0L, static_cast<short>(0), true) == 23 ? 0 : 1;
}
