#pragma once

#include <variant>

namespace FlashCpp {

// Value-oriented variant extraction for types where a default-constructed
// sentinel is a natural "not present" value (e.g. std::string_view{}).
template <typename T, typename... Ts>
[[nodiscard]] constexpr T get_if(const std::variant<Ts...>& value, T default_value) {
	if (const auto* result = std::get_if<T>(&value)) {
		return *result;
	}
	return default_value;
}

template <typename T, typename... Ts>
[[nodiscard]] constexpr T get_if(const std::variant<Ts...>& value) {
	return get_if<T>(value, T{});
}

} // namespace FlashCpp
