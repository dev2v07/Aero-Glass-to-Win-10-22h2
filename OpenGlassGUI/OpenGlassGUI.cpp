#include "pch.h"
#include "OpenGlassGUI.hpp"
#include "MainFrame.hpp"
#include "Elevation.hpp"
#include "ConfigurationMigration.hpp"
#include "resource.h"
#include <wx/cmdline.h>

namespace
{
	bool EnsureOpenGlassHostServiceRunning()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			return false;
		}

		SC_HANDLE service = OpenServiceW(
			manager,
			L"OpenGlassHost",
			SERVICE_QUERY_STATUS | SERVICE_START
		);
		if (!service)
		{
			CloseServiceHandle(manager);
			return false;
		}

		SERVICE_STATUS_PROCESS status{};
		DWORD bytesNeeded = 0;
		if (!QueryServiceStatusEx(
			service,
			SC_STATUS_PROCESS_INFO,
			reinterpret_cast<LPBYTE>(&status),
			sizeof(status),
			&bytesNeeded
		))
		{
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}

		if (status.dwCurrentState == SERVICE_STOPPED)
		{
			if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
			{
				CloseServiceHandle(service);
				CloseServiceHandle(manager);
				return false;
			}
		}

		const ULONGLONG deadline = GetTickCount64() + 5000;
		do
		{
			if (!QueryServiceStatusEx(
				service,
				SC_STATUS_PROCESS_INFO,
				reinterpret_cast<LPBYTE>(&status),
				sizeof(status),
				&bytesNeeded
			))
			{
				break;
			}
			if (status.dwCurrentState == SERVICE_RUNNING)
			{
				CloseServiceHandle(service);
				CloseServiceHandle(manager);
				return true;
			}
			if (status.dwCurrentState == SERVICE_STOPPED)
			{
				break;
			}
			Sleep(100);
		} while (GetTickCount64() < deadline);

		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		return false;
	}

	bool SynchronizeInstalledOpenGlassRuntime()
	{
		WCHAR modulePath[MAX_PATH]{};
		const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
		if (length == 0 || length >= ARRAYSIZE(modulePath))
		{
			return false;
		}

		std::wstring sourceDirectory(modulePath, length);
		const auto separator = sourceDirectory.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
		{
			return false;
		}
		sourceDirectory.resize(separator);

		const std::wstring sourceHost = sourceDirectory + L"\\OpenGlassHost.exe";
		const std::wstring sourceDll = sourceDirectory + L"\\OpenGlass.dll";
		const std::wstring installDirectory = L"C:\\Program Files\\OpenGlass";
		const std::wstring installedHost = installDirectory + L"\\OpenGlassHost.exe";
		const std::wstring installedDll = installDirectory + L"\\OpenGlass.dll";
		const std::wstring backupHost = installDirectory + L"\\OpenGlassHost.exe.previous-aeroglass-v145";
		const std::wstring backupDll = installDirectory + L"\\OpenGlass.dll.previous-aeroglass-v145";

		if (GetFileAttributesW(sourceHost.c_str()) == INVALID_FILE_ATTRIBUTES ||
			GetFileAttributesW(sourceDll.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			return false;
		}
		if (_wcsicmp(sourceHost.c_str(), installedHost.c_str()) == 0 &&
			_wcsicmp(sourceDll.c_str(), installedDll.c_str()) == 0)
		{
			return EnsureOpenGlassHostServiceRunning();
		}

		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			return false;
		}
		SC_HANDLE service = OpenServiceW(manager, L"OpenGlassHost", SERVICE_QUERY_STATUS | SERVICE_STOP);
		if (!service)
		{
			CloseServiceHandle(manager);
			return false;
		}

		SERVICE_STATUS_PROCESS status{};
		DWORD bytesNeeded = 0;
		if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
		{
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}

		if (status.dwCurrentState != SERVICE_STOPPED)
		{
			SERVICE_STATUS ignored{};
			if (!ControlService(service, SERVICE_CONTROL_STOP, &ignored) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
			{
				CloseServiceHandle(service);
				CloseServiceHandle(manager);
				return false;
			}
			const ULONGLONG deadline = GetTickCount64() + 5000;
			do
			{
				if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
				{
					CloseServiceHandle(service);
					CloseServiceHandle(manager);
					return false;
				}
				if (status.dwCurrentState == SERVICE_STOPPED)
				{
					break;
				}
				Sleep(100);
			} while (GetTickCount64() < deadline);
			if (status.dwCurrentState != SERVICE_STOPPED)
			{
				CloseServiceHandle(service);
				CloseServiceHandle(manager);
				return false;
			}
		}

		const bool hostBackupExists = GetFileAttributesW(backupHost.c_str()) != INVALID_FILE_ATTRIBUTES;
		const bool dllBackupExists = GetFileAttributesW(backupDll.c_str()) != INVALID_FILE_ATTRIBUTES;
		bool hostMoved = false;
		bool dllMoved = false;
		if (!hostBackupExists && GetFileAttributesW(installedHost.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			hostMoved = MoveFileExW(installedHost.c_str(), backupHost.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
		}
		else
		{
			hostMoved = hostBackupExists;
		}
		if (!dllBackupExists && GetFileAttributesW(installedDll.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			dllMoved = MoveFileExW(installedDll.c_str(), backupDll.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
		}
		else
		{
			dllMoved = dllBackupExists;
		}

		const bool copiedHost = hostMoved && CopyFileW(sourceHost.c_str(), installedHost.c_str(), FALSE) != FALSE;
		const bool copiedDll = dllMoved && CopyFileW(sourceDll.c_str(), installedDll.c_str(), FALSE) != FALSE;
		if (!copiedHost || !copiedDll)
		{
			if (copiedHost) DeleteFileW(installedHost.c_str());
			if (copiedDll) DeleteFileW(installedDll.c_str());
			if (!hostBackupExists && hostMoved) MoveFileExW(backupHost.c_str(), installedHost.c_str(), MOVEFILE_WRITE_THROUGH);
			if (!dllBackupExists && dllMoved) MoveFileExW(backupDll.c_str(), installedDll.c_str(), MOVEFILE_WRITE_THROUGH);
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}

		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		return EnsureOpenGlassHostServiceRunning();
	}

}

// IMPLEMENT_APP must be in global scope
IMPLEMENT_APP(OpenGlass::OpenGlassApp)

namespace OpenGlass
{
	UINT OpenClassicStartMenuMessage()
	{
		static const UINT message=RegisterWindowMessageW(L"AeroGlass.OpenClassicStartMenu");
		return message;
	}

	void OpenGlassApp::OnInitCmdLine(wxCmdLineParser& parser)
	{
		wxApp::OnInitCmdLine(parser);
		parser.AddLongOption(
			L"elevated-pipe",
			L"internal elevation handshake pipe",
			wxCMD_LINE_VAL_STRING,
			wxCMD_LINE_HIDDEN
		);
		parser.AddLongSwitch(
			L"classic-start-menu",
			L"open the embedded Aero Glass Start Menu settings page",
			wxCMD_LINE_HIDDEN
		);
		parser.AddLongSwitch(
			L"sync-runtime",
			L"synchronize the built runtime with the installed service",
			wxCMD_LINE_HIDDEN
		);
	}

	bool OpenGlassApp::OnInit()
	{
		m_openClassicStartMenu=wcsstr(GetCommandLineW(),L"--classic-start-menu")!=nullptr;
		const bool syncRuntime = wcsstr(GetCommandLineW(), L"--sync-runtime") != nullptr;
		MSWEnableDarkMode(wxApp::DarkMode_Auto);
		if (!wxApp::OnInit())
			return false;

		const auto startup = Elevation::PrepareElevatedStartup();
		if (!startup.continueStartup)
		{
			return false;
		}

		// Machine settings and the schema migration are shared; do not allow two
		// target-user editors to race in the same interactive session.
		m_singleInstanceChecker.Create(L"AeroGlass.SingleInstance");
		if (m_singleInstanceChecker.IsAnotherRunning())
		{
			HWND existing=FindWindowW(nullptr,L"AERO GLASS (Administrator)");
			if (!existing) existing=FindWindowW(nullptr,L"AERO GLASS");
			if (existing) SendMessageW(existing,OpenClassicStartMenuMessage(),0,0);
			return false;
		}

		if (!ConfigurationMigration::EnsureCanonicalConfiguration(startup.userSid))
			return false;

		if (syncRuntime)
		{
			const bool synchronized = SynchronizeInstalledOpenGlassRuntime();
			if (!synchronized)
			{
				wxMessageBox(L"Не удалось обновить установленный runtime Aero.", L"AERO GLASS", wxOK | wxICON_ERROR);
			}
			return false;
		}

		// The GUI writes settings immediately, but the DWM runtime can only
		// consume them while the installed host service is alive. Recover a
		// stopped service before opening the editor. When this is a development
		// build outside the installed directory, synchronize the service pair
		// first so atlas/theme changes use the same v145 runtime that built the UI.
		if (!SynchronizeInstalledOpenGlassRuntime())
		{
			EnsureOpenGlassHostServiceRunning();
		}

		MainFrame* frame = new MainFrame(L"AERO GLASS", startup.userSid);
		if (frame->IsInitializationCanceled())
		{
			frame->Destroy();
			return false;
		}
		frame->Show(true);
		if (m_openClassicStartMenu)
			frame->OpenClassicStartMenuPage();
		return true;
	}
}
