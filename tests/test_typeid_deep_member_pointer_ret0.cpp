struct Owner {};

#define STARS_8 * * * * * * * *
#define STARS_32 STARS_8 STARS_8 STARS_8 STARS_8
#define STARS_128 STARS_32 STARS_32 STARS_32 STARS_32
#define STARS_256 STARS_128 STARS_128

using Deep = int (Owner::* STARS_256)() noexcept;
using DeepThrowing = int (Owner::* STARS_256)();
using OneMore = int (Owner::* STARS_256 *)() noexcept;

int main() {
	const void* identities[] = {typeid(Deep), typeid(Deep const&), typeid(DeepThrowing), typeid(OneMore)};
	if (identities[0] != identities[1]) {
		return 1;
	}
	if (identities[0] == identities[2] || identities[0] == identities[3]) {
		return 2;
	}
	return 0;
}
