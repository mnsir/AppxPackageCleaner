#include <array>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

//#include <Windows.h>
#include <wtypes.h>
#include <minwindef.h>
#include <WinUser.h>
#include <CommCtrl.h>
#include <shlobj.h>

#include "engine.h"
#include "json.hpp"
#include "utils.h"

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
	constexpr auto command = "Get-AppxPackage -AllUsers -PackageTypeFilter Bundle | Select-Object Name, PackageFamilyName, PackageFullName | ConvertTo-Json"sv;
	return RunCommand(BuildCommandLine(command), GetCurDir(), true);
}


void RemoveAppxPackage(std::string_view name)
{
	constexpr auto fmt = R"(Get-AppxPackage -AllUsers -PackageTypeFilter Bundle -name "*{}*" | Remove-AppxPackage -AllUsers)"sv;
	const auto command = std::format(fmt, name);
	RunCommand(BuildCommandLine(command), GetCurDir(), true);
}


std::vector<std::map<std::string, std::string>> ParseJson(std::string jsonAsStr)
{
	std::istringstream ss(std::move(jsonAsStr));
	nlohmann::json json;
	ss >> json;
	auto view = json | std::views::transform([](auto&& item) {return item.get<std::map<std::string, std::string>>(); });
	return { view.begin(), view.end() };
}


std::vector<std::map<std::string, std::string>> packageList;

auto colName = std::to_array("Name");
auto colPackageFullName = std::to_array("PackageFullName");


constexpr auto idUpdate = 1;
constexpr auto idRemove = 2;
constexpr auto idGrid = 3;
constexpr auto idList = 4;

constexpr auto windowWidth = 800;
constexpr auto windowHeight = 450;

constexpr auto buttonWidth = 80;
constexpr auto buttonHeight = 25;

constexpr auto margin = 10;


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		auto [b1, b2, g1, g2, l1, l2] = DivideSegment(windowHeight, margin, 15, buttonHeight, 0.5);

		CreateWindow("Button", "Update", WS_CHILD | WS_VISIBLE | BS_FLAT, 10, b1, buttonWidth, b2, hWnd, (HMENU)idUpdate, NULL, NULL);
		CreateWindow("Button", "Remove", WS_CHILD | WS_VISIBLE | BS_FLAT, 100, b1, buttonWidth, b2, hWnd, (HMENU)idRemove, NULL, NULL);

		CreateWindow(WC_LISTVIEW, "", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT, 10, g1, 760, g2, hWnd, (HMENU)idGrid, NULL, NULL);

		CreateWindow(WC_LISTBOX, "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_STANDARD, 10, l1, 760, l2, hWnd, (HMENU)idList, NULL, NULL);
		
		EnableWindow(GetDlgItem(hWnd, idRemove), FALSE);

		SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(idUpdate, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hWnd, idUpdate)));

		break;
	}
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);

		auto pGrid = GetDlgItem(hWnd, idGrid);
		switch (wmId)
		{
		case idUpdate:
		{
			packageList = ParseJson(GetAppxPackage());
			
			// Clear all columns and items from the list view control
			ListView_DeleteAllItems(pGrid);
			for (auto count = Header_GetItemCount(ListView_GetHeader(pGrid)); count; --count)
			{
				ListView_DeleteColumn(pGrid, 0);
			}

			// Add columns based on the keys in the first package map
			if (!packageList.empty())
			{
				int i = 0;
				auto&& lvCol = LV_COLUMN{ .mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, .fmt = LVCFMT_LEFT, .cx = 150 };
				for (auto&& key : packageList.front() | std::views::keys)
				{
					lvCol.pszText = va(key);
					ListView_InsertColumn(pGrid, i++, &lvCol);
				}
			}
			// Add items to the list view control based on packageList
			for (int iCol = 0; iCol < packageList.size(); iCol++)
			{
				auto&& package = packageList[iCol];
				for (int iRow = 0; auto&& value : package | std::views::values)
				{
					if (iRow == 0)
					{
						auto&& lv = LVITEM{ .mask = LVIF_TEXT, .iItem = iCol, .iSubItem = iRow, .pszText = va(value) };
						ListView_InsertItem(pGrid, &lv);
					}
					else
					{
						ListView_SetItemText(pGrid, iCol, iRow, value.data());
					}
					++iRow;
				}
			}
			// Resize the columns to fit the contents
			int colCount = Header_GetItemCount(ListView_GetHeader(pGrid));
			for (int i = 0; i < colCount; i++)
			{
				ListView_SetColumnWidth(pGrid, i, LVSCW_AUTOSIZE);
			}

			break;
		}
		case idRemove:
		{
			if (auto i = ListView_GetSelectionMark(pGrid); i != LB_ERR)
			{
				RemoveAppxPackage(packageList[i][colName.data()]);
				// Remove the selected item from the list
				ListView_DeleteItem(pGrid, i);
				packageList.erase(packageList.begin() + i);
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
		int width = LOWORD(lParam);
		int height = HIWORD(lParam);

		auto [b1, b2, yPos, g2, l1, l2] = DivideSegment(height, margin, 15, buttonHeight, 0.5);

		auto pGrid = GetDlgItem(hWnd, idGrid);
		MoveWindow(pGrid, 10, yPos, width - 20, g2, TRUE);
		auto pList = GetDlgItem(hWnd, idList);
		MoveWindow(pList, 10, l1, width - 20, l2, TRUE);
		break;
	}
	case WM_NOTIFY:
	{
		auto lpnmh = reinterpret_cast<LPNMHDR>(lParam);
		if (lpnmh->idFrom == idGrid && lpnmh->code == LVN_ITEMCHANGED)
		{
			auto* pNMLV = reinterpret_cast<NMLISTVIEW*>(lParam);
			if ((pNMLV->uNewState & LVIS_SELECTED) != 0)
			{
				EnableWindow(GetDlgItem(hWnd, idRemove), TRUE);
			}
			else
			{
				EnableWindow(GetDlgItem(hWnd, idRemove), FALSE);
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
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	const WNDCLASSEX wcex = {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = hInstance,
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
		.lpszClassName = "PackageManagerClass"
	};
	RegisterClassEx(&wcex);

	const HWND hWnd = CreateWindow("PackageManagerClass", "AppxPackageCleaner", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);
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