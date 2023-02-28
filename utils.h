#pragma once

#include <array>
#include <string_view>


inline auto va(std::string_view sv)
{
	static std::array<char, 256> arr;
	auto&& [in, out] = std::ranges::copy(sv, arr.begin());
	*out = '\0';
	return arr.data();
}



template <std::integral T, typename... Args>
constexpr auto DivideSegment(T total_length, T margin, T interval, Args... parts)
{
	constexpr auto count = sizeof...(parts) + 1;
	static_assert(count > 1, "Invalid total parts sizes count");

	std::array<T, count * 2> v;
	size_t i = 0;
	size_t num = 0;
	T now = 0;
	auto iteration = [&](T part)
	{
		v[i++] = margin + num++ * interval + now;
		v[i++] = part;
		now += part;
	};

	auto sum = total_length - margin - margin - static_cast<T>(count - 1) * interval;
	auto tr = [sum](auto part) { return static_cast<T>(part * (std::is_floating_point_v<decltype(part)> ? sum : 1)); };

	(iteration(tr(parts)), ...);
	v[i++] = margin + num * interval + now;
	v[i] = sum - now;

	return v;
}