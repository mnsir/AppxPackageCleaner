#include <array>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <CommCtrl.h>

#include "json.hpp"

char* va(std::string_view sv)
{
	static std::array<char, 256> arr;
	auto it = std::ranges::copy(sv, arr.begin());
	*it.out = '\0';
	return arr.data();
}


std::string GetAppxPackage()
{
	std::string res;

	SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };

	HANDLE hReadPipe, hWritePipe;
	if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
	{
		//std::cerr << "Failed to create pipes" << std::endl;
		return res;
	}

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.hStdError = hWritePipe;
	si.hStdOutput = hWritePipe;
	si.dwFlags |= STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi = {};

	auto command = std::to_array("powershell.exe -Command \"& {Get-AppxPackage -AllUsers -PackageTypeFilter Bundle | Select-Object Name, PackageFullName | ConvertTo-Json}\"");

	if (!CreateProcess(NULL,
		command.data(),
		NULL,
		NULL,
		TRUE,
		CREATE_NO_WINDOW,
		NULL,
		NULL,
		&si,
		&pi)) {
		//std::cerr << "Failed to create process" << std::endl;
		return res;
	}

	CloseHandle(hWritePipe);

	DWORD dwRead;
	std::array<char, 4096> buf{};
	while (ReadFile(hReadPipe, buf.data(), buf.size(), &dwRead, NULL) && dwRead > 0) {
		res.append(buf.data(), dwRead);
	}

	CloseHandle(hReadPipe);
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return res;
}

void RemoveAppxPackage(std::string name)
{
	using namespace std::string_view_literals;

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags |= STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi = {};

	constexpr auto fmt = R"(powershell.exe -Command "& {{Get-AppxPackage -AllUsers -PackageTypeFilter Bundle -name "*{}*" | Remove-AppxPackage -AllUsers}}")"sv;
	auto command = std::format(fmt, name);

	if (!CreateProcess(NULL,
		command.data(),
		NULL,
		NULL,
		TRUE,
		CREATE_NO_WINDOW,
		NULL,
		NULL,
		&si,
		&pi)) {
		//std::cerr << "Failed to create process" << std::endl;
		return;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}


struct PackageInfo
{
	std::string name;
	std::string packageFullName;
};


std::vector<PackageInfo> ParseJson(std::string jsonAsStr)
{
	std::istringstream ss(std::move(jsonAsStr));
	nlohmann::json json;
	ss >> json;
	auto view = json | std::views::transform([](auto&& item) {return PackageInfo{ .name = item["Name"], .packageFullName = item["PackageFullName"] }; });
	return { view.begin(), view.end() };
}


std::vector<PackageInfo> packageList;
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

		LVCOLUMN lvc = { 0 };
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
				lv.pszText = va(packageList[i].name);
				lv.iItem = i;
				lv.iSubItem = 0;
				ListView_InsertItem(dataGrid, &lv);

				ListView_SetItemText(dataGrid, i, 1, packageList[i].packageFullName.data());
			}
			// Automatically resize the first column to fit the contents
			ListView_SetColumnWidth(dataGrid, 0, LVSCW_AUTOSIZE);
			break;
		}
		case idRemove:
		{
			if (auto i = ListView_GetSelectionMark(dataGrid); i != LB_ERR)
			{
				RemoveAppxPackage(packageList[i].name);
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
	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszClassName = "PackageManagerClass";
	RegisterClassEx(&wcex);

	HWND hWnd = CreateWindow("PackageManagerClass", "Package Manager", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 800, 450, NULL, NULL, hInstance, NULL);
	if (!hWnd)
	{
		return 1;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}