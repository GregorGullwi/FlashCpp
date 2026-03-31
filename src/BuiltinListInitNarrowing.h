#pragma once

#include "ConstExprEvaluator.h"
#include <cmath>
#include <limits>
#include <optional>

namespace BuiltinListInitNarrowing {

struct IntegerConstantValue {
	bool is_signed = true;
	long long signed_value = 0;
	unsigned long long unsigned_value = 0;
};

inline bool isBraceConstructedBuiltin(const ConstructorCallNode& ctor_call) {
	return ctor_call.called_from().value() == "{"sv;
}

inline TypeCategory effectiveScalarCategory(TypeCategory category, TypeIndex type_index) {
	if (category == TypeCategory::Enum) {
		if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
			if (const EnumTypeInfo* enum_info = type_info->getEnumInfo()) {
				return enum_info->underlying_type;
			}
		}
	}
	return category;
}

inline TypeCategory effectiveScalarCategory(const TypeSpecifierNode& type_spec) {
	return effectiveScalarCategory(type_spec.category(), type_spec.type_index());
}

inline TypeCategory effectiveScalarCategory(const ConstExpr::EvalResult& result) {
	if (result.exact_type.has_value()) {
		return effectiveScalarCategory(*result.exact_type);
	}
	if (result.is_uint()) {
		return TypeCategory::UnsignedLongLong;
	}
	if (std::get_if<double>(&result.value)) {
		return TypeCategory::Double;
	}
	if (std::get_if<bool>(&result.value)) {
		return TypeCategory::Bool;
	}
	return TypeCategory::LongLong;
}

inline bool isIntegerLikeCategory(TypeCategory category) {
	return category == TypeCategory::Bool || is_integer_type(category);
}

struct IntegerRangeInfo {
	bool is_signed = false;
	unsigned bits = 0;
};

inline std::optional<IntegerRangeInfo> getIntegerRangeInfo(TypeCategory category) {
	if (category == TypeCategory::Bool) {
		return IntegerRangeInfo{false, 1};
	}
	if (!is_integer_type(category)) {
		return std::nullopt;
	}
	return IntegerRangeInfo{isSignedType(category), static_cast<unsigned>(get_type_size_bits(category))};
}

inline unsigned long long getUnsignedRangeMax(const IntegerRangeInfo& info) {
	if (info.bits == 0) {
		return 0;
	}
	if (info.bits >= 64) {
		return std::numeric_limits<unsigned long long>::max();
	}
	return (1ULL << info.bits) - 1ULL;
}

inline long long getSignedRangeMax(const IntegerRangeInfo& info) {
	if (info.bits == 0) {
		return 0;
	}
	if (info.bits >= 64) {
		return std::numeric_limits<long long>::max();
	}
	return (1LL << (info.bits - 1)) - 1LL;
}

inline long long getSignedRangeMin(const IntegerRangeInfo& info) {
	if (info.bits == 0) {
		return 0;
	}
	if (info.bits >= 64) {
		return std::numeric_limits<long long>::min();
	}
	return -(1LL << (info.bits - 1));
}

inline bool integerSourceRangeFitsTarget(TypeCategory source, TypeCategory target) {
	auto source_info = getIntegerRangeInfo(source);
	auto target_info = getIntegerRangeInfo(target);
	if (!source_info.has_value() || !target_info.has_value()) {
		return false;
	}

	if (source_info->is_signed) {
		return target_info->is_signed && target_info->bits >= source_info->bits;
	}

	if (!target_info->is_signed) {
		return target_info->bits >= source_info->bits;
	}

	return target_info->bits > source_info->bits;
}

inline bool extractIntegerConstantValue(const ConstExpr::EvalResult& result, IntegerConstantValue& constant) {
	if (const auto* bool_value = std::get_if<bool>(&result.value)) {
		constant.is_signed = false;
		constant.unsigned_value = *bool_value ? 1ULL : 0ULL;
		return true;
	}
	if (const auto* signed_value = std::get_if<long long>(&result.value)) {
		constant.is_signed = true;
		constant.signed_value = *signed_value;
		return true;
	}
	if (const auto* unsigned_value = std::get_if<unsigned long long>(&result.value)) {
		constant.is_signed = false;
		constant.unsigned_value = *unsigned_value;
		return true;
	}
	return false;
}

inline bool integerConstantFitsTarget(const IntegerConstantValue& constant, TypeCategory target) {
	auto target_info = getIntegerRangeInfo(target);
	if (!target_info.has_value()) {
		return false;
	}

	if (constant.is_signed) {
		if (target_info->is_signed) {
			return constant.signed_value >= getSignedRangeMin(*target_info) &&
				   constant.signed_value <= getSignedRangeMax(*target_info);
		}
		return constant.signed_value >= 0 &&
			   static_cast<unsigned long long>(constant.signed_value) <= getUnsignedRangeMax(*target_info);
	}

	if (target_info->is_signed) {
		return constant.unsigned_value <= static_cast<unsigned long long>(getSignedRangeMax(*target_info));
	}
	return constant.unsigned_value <= getUnsignedRangeMax(*target_info);
}

template <typename FloatT>
inline bool isIntegerConstantExactlyRepresentableInFloating(const IntegerConstantValue& constant) {
	if (constant.is_signed) {
		FloatT converted = static_cast<FloatT>(constant.signed_value);
		if (!std::isfinite(converted)) {
			return false;
		}
		return static_cast<long long>(converted) == constant.signed_value;
	}

	FloatT converted = static_cast<FloatT>(constant.unsigned_value);
	if (!std::isfinite(converted)) {
		return false;
	}
	return static_cast<unsigned long long>(converted) == constant.unsigned_value;
}

inline bool integerConstantExactlyRepresentableInFloatingTarget(const IntegerConstantValue& constant, TypeCategory target) {
	switch (target) {
	case TypeCategory::Float:
		return isIntegerConstantExactlyRepresentableInFloating<float>(constant);
	case TypeCategory::Double:
		return isIntegerConstantExactlyRepresentableInFloating<double>(constant);
	case TypeCategory::LongDouble:
		return isIntegerConstantExactlyRepresentableInFloating<long double>(constant);
	default:
		return false;
	}
}

inline int floatingRank(TypeCategory category) {
	switch (category) {
	case TypeCategory::Float:
		return 1;
	case TypeCategory::Double:
		return 2;
	case TypeCategory::LongDouble:
		return 3;
	default:
		return 0;
	}
}

template <typename FloatT>
inline bool isFloatingValueExactlyRepresentableAs(double value) {
	FloatT converted = static_cast<FloatT>(value);
	if (!std::isfinite(value) || !std::isfinite(static_cast<double>(converted))) {
		return false;
	}
	return static_cast<double>(converted) == value;
}

inline bool isFloatingValueExactlyRepresentableInTarget(double value, TypeCategory target) {
	switch (target) {
	case TypeCategory::Float:
		return isFloatingValueExactlyRepresentableAs<float>(value);
	case TypeCategory::Double:
		return isFloatingValueExactlyRepresentableAs<double>(value);
	case TypeCategory::LongDouble:
		return isFloatingValueExactlyRepresentableAs<long double>(value);
	default:
		return false;
	}
}

inline bool isNarrowingConversion(
	TypeCategory source,
	TypeCategory target,
	const std::optional<ConstExpr::EvalResult>& constant_value) {
	if (source == TypeCategory::Invalid || target == TypeCategory::Invalid) {
		return false;
	}

	if (is_floating_point_type(source) && isIntegerLikeCategory(target)) {
		return true;
	}

	if (isIntegerLikeCategory(source) && is_floating_point_type(target)) {
		if (!constant_value.has_value()) {
			return true;
		}
		IntegerConstantValue constant;
		return !extractIntegerConstantValue(*constant_value, constant) ||
			   !integerConstantExactlyRepresentableInFloatingTarget(constant, target);
	}

	if (is_floating_point_type(source) && is_floating_point_type(target)) {
		if (floatingRank(target) >= floatingRank(source)) {
			return false;
		}
		if (!constant_value.has_value()) {
			return true;
		}
		const double* floating_value = std::get_if<double>(&constant_value->value);
		return floating_value == nullptr ||
			   !isFloatingValueExactlyRepresentableInTarget(*floating_value, target);
	}

	if (isIntegerLikeCategory(source) && isIntegerLikeCategory(target)) {
		if (integerSourceRangeFitsTarget(source, target)) {
			return false;
		}
		if (!constant_value.has_value()) {
			return true;
		}
		IntegerConstantValue constant;
		return !extractIntegerConstantValue(*constant_value, constant) ||
			   !integerConstantFitsTarget(constant, target);
	}

	return false;
}

inline bool isNarrowingConversion(
	TypeCategory source,
	TypeCategory target,
	const ConstExpr::EvalResult& constant_value) {
	return isNarrowingConversion(
		source,
		target,
		std::optional<ConstExpr::EvalResult>{constant_value});
}

} // namespace BuiltinListInitNarrowing
