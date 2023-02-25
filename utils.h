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
