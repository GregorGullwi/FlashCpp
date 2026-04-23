// Scenario A: type pack that appears only in explicit template arguments, not in the
// function signature.  sizeof...(Bonus) must equal the number of explicit type args
// beyond the first (T), regardless of what the single function parameter is.
//
// with_bonus_pack<int, float, double>(3):
//   T=int (deduced from arg), Bonus={float,double} (explicit-only)
//   returns sizeof...(Bonus) + a  =  2 + 3  =  5

template<typename T, typename... Bonus>
int with_bonus_pack(T a) { return static_cast<int>(sizeof...(Bonus)) + static_cast<int>(a); }

int main() {
	int a = with_bonus_pack<int, float, double>(3);
	if (a != 5) return 1;
	return 0;
}
