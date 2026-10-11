#include "WindowPlacement.h"

#include <windows.h>

#include <cwchar>
#include <cstdlib>
#include <vector>

namespace netball
{
	namespace
	{
		struct MonitorEntry
		{
			RECT rect;
			int displayNumber;
		};

		BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
		{
			auto* list = reinterpret_cast<std::vector<MonitorEntry>*>(data);
			MONITORINFOEXW info{};
			info.cbSize = sizeof(info);
			if (!GetMonitorInfoW(monitor, &info))
				return TRUE;
			int number = 0;
			// szDevice looks like \\.\DISPLAY3
			const wchar_t* tail = std::wcsstr(info.szDevice, L"DISPLAY");
			if (tail)
				number = _wtoi(tail + 7);
			list->push_back({ info.rcMonitor, number });
			return TRUE;
		}

		struct WindowSearch
		{
			DWORD processId;
			HWND found;
		};

		BOOL CALLBACK FindOwnWindow(HWND window, LPARAM data)
		{
			auto* search = reinterpret_cast<WindowSearch*>(data);
			DWORD id = 0;
			GetWindowThreadProcessId(window, &id);
			if (id == search->processId && IsWindowVisible(window) &&
				GetWindow(window, GW_OWNER) == nullptr)
			{
				search->found = window;
				return FALSE;
			}
			return TRUE;
		}
	}

	bool FillMonitor(const int displayNumber)
	{
		std::vector<MonitorEntry> monitors;
		EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
			reinterpret_cast<LPARAM>(&monitors));
		const RECT* target = nullptr;
		for (const MonitorEntry& monitor : monitors)
			if (monitor.displayNumber == displayNumber)
				target = &monitor.rect;
		if (!target && displayNumber >= 1 &&
			displayNumber <= static_cast<int>(monitors.size()))
			target = &monitors[displayNumber - 1].rect;
		if (!target)
			return false;

		WindowSearch search{ GetCurrentProcessId(), nullptr };
		EnumWindows(FindOwnWindow, reinterpret_cast<LPARAM>(&search));
		if (!search.found)
			return false;

		SetWindowLongPtrW(search.found, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(search.found, HWND_TOP, target->left, target->top,
			target->right - target->left, target->bottom - target->top,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		return true;
	}
}
