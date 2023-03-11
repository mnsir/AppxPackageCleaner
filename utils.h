#pragma once

#include <array>
#include <string_view>


inline auto va(std::string_view sv)
{
	static std::array<char, 1024> arr;
	auto&& [in, out] = std::ranges::copy(sv, arr.begin());
	*out = '\0';
	return arr.data();
}


constexpr auto g_margin = 10;
constexpr auto g_interval = 15;

struct Segment
{
	Segment() = default;
	explicit Segment(int length)
		: pos{ g_margin }
		, len{ length - g_margin - g_margin }
	{
	}
	int pos;
	int len;
};

struct Absolute
{
	explicit Absolute(int value) : val{ value } {}
	int val;
};

struct Relative
{
	explicit Relative(int value) : val{ value } {}
	int val;
};

struct Combiner
{
	Combiner(auto... parts)
		: abs{ 0 }
		, rel{ 0 }
	{
		(Combine(parts), ...);
	}
	void Combine(const Absolute& value) { abs += value.val; }
	void Combine(const Relative& value) { rel += value.val; }


	int abs;
	int rel;
};

struct Converter
{
	Converter(double quotient) : quot{ quotient } {}
	int operator()(const Absolute& value) const { return value.val; }
	int operator()(const Relative& value) const { return lround(quot * value.val); }

	const double quot;
};


auto ConvertToAbsolute(double price, auto... parts)
{
	std::array<int, sizeof...(parts)> arr;
	size_t i = 0;
	Converter convert(price);
	((arr[i++] = convert(parts)), ...);
	return arr;
}

template <typename... Args>
auto DivideSegment(Segment segment, Args... parts)
{
	constexpr int count = sizeof...(parts);
	Combiner combiner(parts...);
	int remainder = segment.len - combiner.abs - (count - 1) * g_interval;
	auto arr = ConvertToAbsolute(remainder * 1.0 / combiner.rel, parts...);
	std::array<Segment, count> res{};
	for (size_t i = 0; i < res.size(); ++i)
	{
		res[i].pos += i * g_interval + segment.pos;
		for (size_t j = 0; j < i; ++j)
		{
			res[i].pos += res[j].len;
		}
		res[i].len = arr[i];
	}
	return res;
}