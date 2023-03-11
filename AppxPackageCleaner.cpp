#include <array>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

#include <Windowsx.h>
#include <wtypes.h>
#include <WinUser.h>
#include <CommCtrl.h>
#include <shlobj.h>

#include "engine.h"
#include "json.hpp"
#include "utils.h"

#undef max


template<typename CharT>
struct std::formatter<std::variant<std::string, int, bool, std::vector<std::string>>, CharT> : std::formatter<std::string, CharT> {
	template<typename FormatContext>
	auto format(const std::variant<std::string, int, bool, std::vector<std::string>>& value, FormatContext& ctx) {
		if (std::holds_alternative<std::string>(value)) {
			return formatter<std::string, CharT>::format(std::get<std::string>(value), ctx);
		}
		else if (std::holds_alternative<int>(value)) {
			return format_to(ctx.out(), "{}", std::get<int>(value));
		}
		else if (std::holds_alternative<bool>(value)) {
			return format_to(ctx.out(), "{}", std::get<bool>(value));
		}
		else if (std::holds_alternative<std::vector<std::string>>(value)) {
			auto& vec = std::get<std::vector<std::string>>(value);
			auto it = vec.begin();
			auto end = vec.end();
			if (it == end) {
				return format_to(ctx.out(), "[]");
			}
			else {
				format_to(ctx.out(), "[");
				while (it != end) {
					formatter<std::string, CharT>::format(*it, ctx);
					++it;
					if (it != end) {
						format_to(ctx.out(), ", ");
					}
				}
				format_to(ctx.out(), "]");
				return ctx.out();
			}
		}
		return ctx.out();
	}
};


using namespace std::string_view_literals;


auto GetPSDir()
{
	std::array<char, 256> arr{};
	const auto n = GetSystemDirectory(arr.data(), arr.size());
	return std::format(R"({}\WindowsPowerShell\v1.0\powershell.exe)"sv, std::string_view(arr.data(), n));
}

std::string GetCurDir()
{
	std::array<char, MAX_PATH> arr{};
	SHGetSpecialFolderPath(nullptr, arr.data(), CSIDL_LOCAL_APPDATA, FALSE);
	return arr.data();
}


auto BuildCommandLine(std::string_view sv)
{
	return std::format("{} -NonInteractive -NoProfile -Command {}"sv, GetPSDir(), sv);
}


std::string GetAppxPackage()
{
	constexpr auto command = "Get-AppxPackage -AllUsers -PackageTypeFilter Bundle | ConvertTo-Json"sv;
	return RunCommand(BuildCommandLine(command), GetCurDir(), true);
}


void RemoveAppxPackage(std::string_view name)
{
	constexpr auto fmt = R"(Get-AppxPackage -AllUsers -PackageTypeFilter Bundle -name "*{}*" | Remove-AppxPackage -AllUsers)"sv;
	const auto command = std::format(fmt, name);
	RunCommand(BuildCommandLine(command), GetCurDir(), false);
}


std::string LoadUpdates(std::string_view packageFamilyName)
{
	constexpr auto fmt = R"((Invoke-WebRequest -UseBasicParsing https://store.rg-adguard.net/api/GetFiles -ContentType "application/x-www-form-urlencoded" -Method POST -Body @{{type='PackageFamilyName';url='{}';ring='RP';lang='ru-RU'}}).Links | Select-Object @{{Name='href'; Expression={{$_.href}}}}, @{{Name='text'; Expression={{$_.outerHTML -replace '<.*?>'}}}} | ConvertTo-Json)"sv;
	const auto command = std::format(fmt, packageFamilyName);
	return RunCommand(BuildCommandLine(command), GetCurDir(), true);
}


auto AddAppxPackage(std::string_view href)
{
	constexpr auto fmt = "Add-AppxPackage -Path '{}'"sv;
	const auto command = std::format(fmt, href);
	return RunCommand(BuildCommandLine(command), GetCurDir(), true);
}


std::vector<std::pair<std::string, std::string>> ParseJson2(std::string jsonAsStr)
{
	std::vector<std::pair<std::string, std::string>> resultMap;

	try {
		auto&& jsonObj = nlohmann::json::parse(std::move(jsonAsStr));

		for (auto&& item : jsonObj)
		{
			resultMap.emplace_back(item["text"], item["href"]);
		}
	}
	catch (const std::exception& e)
	{
		::MessageBox(NULL, e.what(), __FUNCTION__, 0);
	}
	return resultMap;
}


std::vector<std::map<std::string, std::variant<std::string, int, bool, std::vector<std::string>>>> ParseJson3(std::string jsonAsStr)
{
	std::vector<std::map<std::string, std::variant<std::string, int, bool, std::vector<std::string>>>> result;

	try {
		auto parsedJson = nlohmann::json::parse(std::move(jsonAsStr));

		for (auto&& obj : parsedJson)
		{
			std::map<std::string, std::variant<std::string, int, bool, std::vector<std::string>>> item;

			for (auto it = obj.begin(); it != obj.end(); ++it)
			{
				auto&& key = it.key();
				auto&& value = it.value();


				if (value.is_string())
				{
					item[key] = value.get<std::string>();
				}
				else if (value.is_number())
				{
					item[key] = value.get<int>();
				}
				else if (value.is_boolean())
				{
					item[key] = value.get<bool>();
				}
				else if (value.is_array())
				{
					std::vector<std::string> vec;
					for (auto&& str : value)
					{
						vec.push_back(str.get<std::string>());
					}
					item[key] = vec;
				}
			}

			result.push_back(item);
		}
	}
	catch (const std::exception& e)
	{
		::MessageBox(NULL, e.what(), __FUNCTION__, 0);
	}
	return result;
}


std::vector<std::map<std::string, std::variant<std::string, int, bool, std::vector<std::string>>>> packageList;
std::map<std::string, std::vector<std::pair<std::string, std::string>>> updates;

auto colName = std::to_array("Name");
auto colPackageFamilyName = std::to_array("PackageFamilyName");
auto colPackageFullName = std::to_array("PackageFullName");


constexpr auto ID_GET_APPX_PACKAGE = 10;
constexpr auto idRemoveAppxPackage = 2;
constexpr auto idPackagesList = 3;
constexpr auto idLoadPackageUpdates = 5;
constexpr auto idPackageInfo = 7;
constexpr auto idPackageUpdates = 8;
constexpr auto idAddAppxPackage = 9;

auto windowX = CW_USEDEFAULT;
auto windowY = 0;
auto windowWidth = 800;
auto windowHeight = 450;

constexpr auto buttonWidth = 80;
constexpr auto buttonHeight = 25;

constexpr auto buttonWidthLong = 120;

constexpr auto margin = 10;
constexpr auto interval = 15;


namespace ui
{
	void ListView_Reset(HWND hListView)
	{
		ListView_DeleteAllItems(hListView);
		for (auto count = Header_GetItemCount(ListView_GetHeader(hListView)); count; --count)
			ListView_DeleteColumn(hListView, 0);
	}

	void ListView_CreateColumns(HWND hListView, std::vector<std::string> names)
	{
		auto&& lvCol = LV_COLUMN{ .mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, .fmt = LVCFMT_LEFT, .cx = 0 };
		for (int i = 0; auto && name : names)
		{
			lvCol.pszText = name.data();
			ListView_InsertColumn(hListView, i++, &lvCol);
		}
	}

	void ListView_AppendRow(HWND hListView, std::vector<std::vector<std::string>> table)
	{
		for (auto&& values : table)
		{
			int iRow = ListView_GetItemCount(hListView);
			for (int iCol : std::views::iota(0, static_cast<int>(values.size())))
			{
				if (iCol == 0)
				{
					auto&& lv = LVITEM{ .mask = LVIF_TEXT, .iItem = iRow, .iSubItem = iCol, .pszText = values[iCol].data() };
					ListView_InsertItem(hListView, &lv);
				}
				else
				{
					ListView_SetItemText(hListView, iRow, iCol, values[iCol].data());
				}
			}
		}
	}

	void ListView_SetAutoSize(HWND hListView)
	{
		for (auto i : std::views::iota(0, Header_GetItemCount(ListView_GetHeader(hListView))))
			ListView_SetColumnWidth(hListView, i, LVSCW_AUTOSIZE);
	}
}


const UINT WM_GET_APPX_PACKAGE = RegisterWindowMessage(TEXT("GET_APPX_PACKAGE"));


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		constexpr auto gridDefault = WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT;
		constexpr auto gridSingleRow = gridDefault | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER;

		auto [left, right] = DivideSegment(Segment{ windowWidth }, Relative{ 3 }, Relative{ 7 });

		auto [g] = DivideSegment(Segment{ windowHeight }, Relative{ 1 });
		CreateWindow(WC_LISTVIEW, "", gridSingleRow, left.pos, g.pos, left.len, g.len, hWnd, (HMENU)idPackagesList, NULL, NULL);

		auto [a, b, c, d] = DivideSegment(Segment{ windowHeight }, Absolute{ buttonHeight }, Relative{ 4 }, Absolute{ buttonHeight }, Relative{ 6 });
		CreateWindow(WC_BUTTON, "Remove", WS_CHILD | WS_VISIBLE | WS_DISABLED, right.pos, a.pos, buttonWidth, a.len, hWnd, (HMENU)idRemoveAppxPackage, NULL, NULL);
		CreateWindow(WC_LISTVIEW, "", gridDefault, right.pos, b.pos, right.len, b.len, hWnd, (HMENU)idPackageInfo, NULL, NULL);

		auto [e, f] = DivideSegment(right, Absolute{ buttonWidthLong }, Absolute{ buttonWidthLong });
		CreateWindow(WC_BUTTON, "Load Updates", WS_CHILD | WS_VISIBLE | WS_DISABLED, e.pos, c.pos, e.len, c.len, hWnd, (HMENU)idLoadPackageUpdates, NULL, NULL);
		CreateWindow(WC_BUTTON, "Install Update", WS_CHILD | WS_VISIBLE | WS_DISABLED, f.pos, c.pos, f.len, c.len, hWnd, (HMENU)idAddAppxPackage, NULL, NULL);

		CreateWindow(WC_LISTVIEW, "", gridSingleRow, right.pos, d.pos, right.len, d.len, hWnd, (HMENU)idPackageUpdates, NULL, NULL);

		HMENU hSysMenu = GetSystemMenu(hWnd, FALSE);
		AppendMenu(hSysMenu, MF_SEPARATOR, 0, nullptr);
		AppendMenu(hSysMenu, MF_STRING, ID_GET_APPX_PACKAGE, "Update Packages List");

		SendMessage(hWnd, WM_GET_APPX_PACKAGE, 0, 0);

		break;
	}
	case WM_SYSCOMMAND:
	{
		switch (LOWORD(wParam))
		{
		case ID_GET_APPX_PACKAGE:
		{
			SendMessage(hWnd, WM_GET_APPX_PACKAGE, 0, 0);
			break;
		}
		default:
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}
		break;
	}
	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case idRemoveAppxPackage:
		{
			auto hPackagesList = GetDlgItem(hWnd, idPackagesList);
			if (auto i = ListView_GetSelectionMark(hPackagesList); i != LB_ERR)
			{
				auto&& valueAsStr = std::format("{}", packageList[i][colName.data()]);
				RemoveAppxPackage(valueAsStr);
				// Remove the selected item from the list
				ListView_DeleteItem(hPackagesList, i);
				packageList.erase(packageList.begin() + i);
			}
			break;
		}
		case idLoadPackageUpdates:
		{
			if (auto i = ListView_GetSelectionMark(GetDlgItem(hWnd, idPackagesList)); i != LB_ERR)
			{
				auto&& package = packageList[i];
				auto&& strPackageFamilyName = std::format("{}", package[colPackageFamilyName.data()]);
				auto&& json = LoadUpdates(strPackageFamilyName);
				auto&& fileNamesToLinks = ParseJson2(json) | std::views::filter([](auto&& v) { return !v.first.ends_with(".BlockMap"); });
				auto&& strName = std::format("{}", package[colName.data()]);
				auto&& update = updates[strName];
				update = std::vector(fileNamesToLinks.begin(), fileNamesToLinks.end());

				std::vector<std::vector<std::string>> table;
				for (auto&& view : std::as_const(update) | std::views::keys)
					table.emplace_back(std::vector({ view }));

				Button_Enable(GetDlgItem(hWnd, idLoadPackageUpdates), FALSE);
				Button_Enable(GetDlgItem(hWnd, idAddAppxPackage), FALSE);
				auto hPackageUpdates = GetDlgItem(hWnd, idPackageUpdates);
				ui::ListView_Reset(hPackageUpdates);
				ui::ListView_CreateColumns(hPackageUpdates, { "" });
				ui::ListView_AppendRow(hPackageUpdates, std::move(table));
				ui::ListView_SetAutoSize(hPackageUpdates);
			}
			break;
		}
		case idAddAppxPackage:
		{
			if (auto i = ListView_GetSelectionMark(GetDlgItem(hWnd, idPackagesList)); i != LB_ERR)
			{
				auto&& package = packageList[i];
				auto&& strName = std::format("{}", package[colName.data()]);
				auto&& update = updates[strName];

				if (auto j = ListView_GetSelectionMark(GetDlgItem(hWnd, idPackageUpdates)); j != LB_ERR)
				{
					auto&& [text, href] = update[j];
					auto str = AddAppxPackage(href);
					if (!str.empty())
						MessageBox(nullptr, str.c_str(), __FUNCTION__, 0);
				}
			}
			break;
		}
		default:
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}
		break;
	}
	case WM_SIZE:
	{
		const int width = LOWORD(lParam);
		const int height = HIWORD(lParam);

		auto [left, right] = DivideSegment(Segment{ width }, Relative{ 3 }, Relative{ 7 });

		auto [g] = DivideSegment(Segment{ height }, Relative{ 1 });
		MoveWindow(GetDlgItem(hWnd, idPackagesList), left.pos, g.pos, left.len, g.len, FALSE);

		auto [a, b, c, d] = DivideSegment(Segment{ height }, Absolute{ buttonHeight }, Relative{ 4 }, Absolute{ buttonHeight }, Relative{ 6 });
		MoveWindow(GetDlgItem(hWnd, idRemoveAppxPackage), right.pos, a.pos, buttonWidth, a.len, FALSE);
		MoveWindow(GetDlgItem(hWnd, idPackageInfo), right.pos, b.pos, right.len, b.len, FALSE);

		auto [e, f] = DivideSegment(right, Absolute{ buttonWidthLong }, Absolute{ buttonWidthLong });
		MoveWindow(GetDlgItem(hWnd, idLoadPackageUpdates), e.pos, c.pos, e.len, c.len, FALSE);
		MoveWindow(GetDlgItem(hWnd, idAddAppxPackage), f.pos, c.pos, f.len, c.len, FALSE);

		MoveWindow(GetDlgItem(hWnd, idPackageUpdates), right.pos, d.pos, right.len, d.len, FALSE);
		break;
	}
	case WM_NOTIFY:
	{
		// ReSharper disable once CppFunctionalStyleCast, CppLocalVariableMayBeConst
		if (auto lpnmh = (LPNMHDR)lParam; lpnmh->idFrom == idPackagesList && lpnmh->code == LVN_ITEMCHANGED)
		{
			if (auto pNMLV = (LPNMLISTVIEW)lParam; (pNMLV->uNewState & LVIS_SELECTED) != 0)
			{
				auto&& package = packageList[pNMLV->iItem];

				Button_Enable(GetDlgItem(hWnd, idRemoveAppxPackage), TRUE);

				{
					auto hPackageInfo = GetDlgItem(hWnd, idPackageInfo);
					ui::ListView_Reset(hPackageInfo);
					ui::ListView_CreateColumns(hPackageInfo, { "Key", "Value" });

					std::vector<std::vector<std::string>> table;
					table.reserve(package.size());
					for (auto&& [key, value] : package)
						table.emplace_back(std::vector({ key, std::format("{}", value) }));

					ui::ListView_AppendRow(hPackageInfo, std::move(table));
					ui::ListView_SetAutoSize(hPackageInfo);
				}
				{
					auto&& packageName = std::format("{}", package[colName.data()]);
					auto&& update = updates[packageName];
					std::vector<std::vector<std::string>> table;
					for (auto&& view : update | std::views::keys)
						table.emplace_back(std::vector({ view }));

					if (!table.empty())
					{
						Button_Enable(GetDlgItem(hWnd, idLoadPackageUpdates), FALSE);
						Button_Enable(GetDlgItem(hWnd, idAddAppxPackage), FALSE);
						auto hPackageUpdates = GetDlgItem(hWnd, idPackageUpdates);
						ui::ListView_Reset(hPackageUpdates);
						ui::ListView_CreateColumns(hPackageUpdates, { "" });
						ui::ListView_AppendRow(hPackageUpdates, std::move(table));
						ui::ListView_SetAutoSize(hPackageUpdates);
					}
					else
					{
						Button_Enable(GetDlgItem(hWnd, idLoadPackageUpdates), TRUE);
						Button_Enable(GetDlgItem(hWnd, idAddAppxPackage), FALSE);
						ui::ListView_Reset(GetDlgItem(hWnd, idPackageUpdates));
					}
				}
			}
			else
			{
				Button_Enable(GetDlgItem(hWnd, idRemoveAppxPackage), FALSE);
				ui::ListView_Reset(GetDlgItem(hWnd, idPackageInfo));
				Button_Enable(GetDlgItem(hWnd, idLoadPackageUpdates), FALSE);
				Button_Enable(GetDlgItem(hWnd, idAddAppxPackage), FALSE);
				ui::ListView_Reset(GetDlgItem(hWnd, idPackageUpdates));
			}
		}
		else if (lpnmh->idFrom == idPackageUpdates && lpnmh->code == LVN_ITEMCHANGED)
		{
			if (auto pNMLV = (LPNMLISTVIEW)lParam; (pNMLV->uNewState & LVIS_SELECTED) != 0)
			{
				Button_Enable(GetDlgItem(hWnd, idAddAppxPackage), TRUE);
			}
			else
			{
				Button_Enable(GetDlgItem(hWnd, idAddAppxPackage), FALSE);
			}
		}
		break;
	}
	case WM_CLOSE:
	{
		DestroyWindow(hWnd);
		break;
	}
	case WM_DESTROY:
	{
		PostQuitMessage(0);
		break;
	}
	default:
	{
		if (msg == WM_GET_APPX_PACKAGE)
		{
			packageList = ParseJson3(GetAppxPackage());

			auto hPackagesList = GetDlgItem(hWnd, idPackagesList);
			ui::ListView_Reset(hPackagesList);

			// Add columns based on the keys in the first package map
			if (!packageList.empty())
				ui::ListView_CreateColumns(hPackagesList, { "" });
			// Add items to the list view control based on packageList

			std::vector<std::vector<std::string>> table;
			for (auto& iRow : packageList)
			{
				auto&& packageName = std::format("{}", iRow[colName.data()]);
				table.emplace_back(std::vector({ std::move(packageName) }));
			}
			ui::ListView_AppendRow(hPackagesList, std::move(table));
			// Resize the columns to fit the contents
			ui::ListView_SetAutoSize(hPackagesList);

			return 0;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	}
	return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	windowWidth = GetSystemMetrics(SM_CXSCREEN) / 2;
	windowHeight = GetSystemMetrics(SM_CYSCREEN) / 2;
	windowX = windowWidth / 2;
	windowY = windowHeight / 2;

	WNDCLASSEX wcex = {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = hInstance,
		.hCursor = LoadCursor(nullptr, IDC_ARROW),
		.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
		.lpszClassName = "PackageManagerClass"
	};
	RegisterClassEx(&wcex);

	HWND hWnd = CreateWindow("PackageManagerClass", "AppxPackageCleaner", WS_OVERLAPPEDWINDOW, windowX, windowY, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);
	if (!hWnd)
	{
		return 1;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return static_cast<int>(msg.wParam);
}