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
	constexpr auto command = "Get-AppxPackage -AllUsers -PackageTypeFilter Bundle | Select-Object Name, PackageFullName | ConvertTo-Json"sv;
	return RunCommand(BuildCommandLine(command), GetCurDir(), true);
}


void RemoveAppxPackage(std::string name)
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
HWND dataGrid;

auto colName = std::to_array("Name");
auto colPackageFullName = std::to_array("PackageFullName");


constexpr auto idUpdate = 1;
constexpr auto idRemove = 2;
constexpr auto idGrid = 3;


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		CreateWindow("Button", "Update", WS_CHILD | WS_VISIBLE | BS_FLAT, 10, 10, 80, 25, hWnd, (HMENU)idUpdate, NULL, NULL);
		CreateWindow("Button", "Remove", WS_CHILD | WS_VISIBLE | BS_FLAT, 100, 10, 80, 25, hWnd, (HMENU)idRemove, NULL, NULL);

		dataGrid = CreateWindow(WC_LISTVIEW, "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | LVS_REPORT, 10, 50, 760, 340, hWnd, (HMENU)idGrid, NULL, NULL);

		LVCOLUMN lvc = {};
		lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
		lvc.fmt = LVCFMT_LEFT;
		lvc.cx = 150;
		lvc.pszText = colName.data();
		ListView_InsertColumn(dataGrid, 0, &lvc);

		lvc.fmt = LVCFMT_LEFT;
		lvc.cx = 600;
		lvc.pszText = colPackageFullName.data();
		ListView_InsertColumn(dataGrid, 1, &lvc);
		EnableWindow(GetDlgItem(hWnd, idRemove), FALSE);
		SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(idUpdate, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hWnd, idUpdate)));
		break;
	}
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		switch (wmId)
		{
		case idUpdate:
		{
			packageList = ParseJson(GetAppxPackage());

			ListView_DeleteAllItems(dataGrid);
			for (int i = 0; i < packageList.size(); i++)
			{
				LVITEM lv = { 0 };
				lv.mask = LVIF_TEXT;
				lv.pszText = va(packageList[i][colName.data()]);
				lv.iItem = i;
				lv.iSubItem = 0;
				ListView_InsertItem(dataGrid, &lv);

				ListView_SetItemText(dataGrid, i, 1, packageList[i][colPackageFullName.data()].data());
			}
			// Automatically resize the first column to fit the contents
			ListView_SetColumnWidth(dataGrid, 0, LVSCW_AUTOSIZE);
			break;
		}
		case idRemove:
		{
			if (auto i = ListView_GetSelectionMark(dataGrid); i != LB_ERR)
			{
				RemoveAppxPackage(packageList[i][colName.data()]);
				// Remove the selected item from the list
				ListView_DeleteItem(dataGrid, i);
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

		MoveWindow(dataGrid, 10, 50, width - 20, height - 60, TRUE);
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

	const HWND hWnd = CreateWindow("PackageManagerClass", "AppxFuckage", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 800, 450, NULL, NULL, hInstance, NULL);
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