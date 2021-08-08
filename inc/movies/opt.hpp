// Copyright (c) 2021 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <optional>

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
concept Optional = is_optional<T>::value;

template <typename T, typename In>
concept VoidPred =
    std::is_same_v<decltype(std::declval<T>()(std::declval<In const&>())),
                   void>;

template <typename Type>
struct add_opt {
	using type = std::optional<std::remove_cvref_t<Type>>;
	using nullopt_t = std::nullopt_t;
};

template <Optional Type>
struct add_opt<Type> {
	using type = Type;
	using nullopt_t = std::nullopt_t;
};

template <typename In, typename Pred>
auto operator>>(std::optional<In> const& in, Pred pred) ->
    typename add_opt<decltype(pred(*in))>::type {
	if (!in) return std::nullopt;
	return pred(*in);
}

template <typename In, VoidPred<In> Pred>
void operator>>(std::optional<In> const& in, Pred pred) {
	if (!in) return;
	pred(*in);
}

template <typename In, typename Def>
auto operator||(std::optional<In> const& in, Def&& def) {
	return in.value_or(std::forward<Def>(def));
}

template <typename In>
std::optional<In> const& operator||(std::optional<In> const& in,
                                    std::optional<In> const& def) {
	if (in) return in;
	return def;
}

template <typename In>
std::optional<In> const& operator||(std::optional<In> const& in,
                                    std::optional<In>& def) {
	if (in) return in;
	return def;
}

template <typename In>
std::optional<In> const& operator||(std::optional<In>& in,
                                    std::optional<In> const& def) {
	if (in) return in;
	return def;
}

template <typename In>
std::optional<In> const& operator||(std::optional<In>& in,
                                    std::optional<In>& def) {
	if (in) return in;
	return def;
}

template <typename In, Optional Def>
auto operator||(std::optional<In> const& in, Def const& def)
    -> std::common_type_t<std::optional<In>, Def> {
	if (in) return in;
	return def;
}
