#include "pch.h"
#include "MainFrame.hpp"
#include <sstream>
#include <tlhelp32.h>

namespace OpenGlass
{
	namespace
	{
		// The embedded Open-Shell settings manager and the injected menu both use
		// the component root. Keeping the compact GUI on a separate \"Settings\"
		// subkey made its values look saved while the shell ignored them.
		constexpr wchar_t kClassicSettingsKey[] = L"Software\\AeroGlass\\StartMenu";
		constexpr wchar_t kClassicRuntimeSettingsKey[] = L"Software\\AeroGlass\\StartMenu";
		std::wstring ReadStringValue(const wchar_t* name);

		std::wstring DefaultStartButtonAsset()
		{
			WCHAR modulePath[MAX_PATH]{};
			const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
			if (length == 0 || length >= ARRAYSIZE(modulePath)) return {};
			return (std::filesystem::path(modulePath).parent_path() / L"Button" / L"start.bmp").wstring();
		}

		void EnsureDefaultStartButtonAsset()
		{
			const auto asset = DefaultStartButtonAsset();
			if (asset.empty() || !std::filesystem::exists(asset) || !ReadStringValue(L"StartButtonPath").empty()) return;
			wil::unique_hkey key;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, kClassicRuntimeSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.put(), nullptr) != ERROR_SUCCESS) return;
			const auto type = std::wstring(L"CustomButton");
			RegSetValueExW(key.get(), L"StartButtonType", 0, REG_SZ, reinterpret_cast<const BYTE*>(type.c_str()), static_cast<DWORD>((type.size() + 1) * sizeof(wchar_t)));
			RegSetValueExW(key.get(), L"StartButtonPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(asset.c_str()), static_cast<DWORD>((asset.size() + 1) * sizeof(wchar_t)));
		}

		std::wstring ReadStringValue(const wchar_t* name)
		{
			wil::unique_hkey key;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kClassicSettingsKey, 0, KEY_READ, key.put()) != ERROR_SUCCESS)
				return {};
			DWORD type = 0, bytes = 0;
			if (RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
				(type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t))
				return {};
			std::wstring value(bytes / sizeof(wchar_t), L'\0');
			if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS)
				return {};
			if (!value.empty() && value.back() == L'\0') value.pop_back();
			return value;
		}

		std::wstring ReadMultiStringValue(const wchar_t* name)
		{
			wil::unique_hkey key;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kClassicSettingsKey, 0, KEY_READ, key.put()) != ERROR_SUCCESS)
				return {};
			DWORD type = 0, bytes = 0;
			if (RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_MULTI_SZ)
				return {};
			std::wstring value(bytes / sizeof(wchar_t), L'\0');
			if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS)
				return {};
			for (auto& ch : value) if (ch == L'\0') ch = L'\n';
			while (!value.empty() && value.back() == L'\n') value.pop_back();
			return value;
		}

		DWORD ReadDwordValue(const wchar_t* name, DWORD fallback)
		{
			wil::unique_hkey key;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kClassicSettingsKey, 0, KEY_READ, key.put()) != ERROR_SUCCESS)
				return fallback;
			DWORD type = 0, value = fallback, bytes = sizeof(value);
			if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) != ERROR_SUCCESS || type != REG_DWORD)
				return fallback;
			return value;
		}

		bool WriteStringValue(HKEY key, const wchar_t* name, const std::wstring& value);
		bool WriteDwordValue(HKEY key, const wchar_t* name, DWORD value);

		int ReadIntOrRadioValue(const wchar_t* name, int fallback, const std::vector<std::wstring>& radioNames)
		{
			constexpr DWORD kMissing = 0xffffffff;
			const DWORD numeric = ReadDwordValue(name, kMissing);
			if (numeric != kMissing) return static_cast<int>(numeric);
			const auto text = ReadStringValue(name);
			for (size_t i = 0; i < radioNames.size(); ++i)
				if (_wcsicmp(text.c_str(), radioNames[i].c_str()) == 0) return static_cast<int>(i);
			return fallback;
		}

		bool WriteIntOrRadioValue(HKEY key, const wchar_t* name, int value, const std::vector<std::wstring>& radioNames)
		{
			if (value >= 0 && static_cast<size_t>(value) < radioNames.size())
				return WriteStringValue(key, name, radioNames[static_cast<size_t>(value)]);
			return WriteDwordValue(key, name, static_cast<DWORD>(value));
		}

		std::filesystem::path OpenGlassStartMenuExecutable()
		{
			WCHAR modulePath[MAX_PATH]{};
			const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
			if (length == 0 || length >= ARRAYSIZE(modulePath)) return {};
			return std::filesystem::path(modulePath).parent_path() / L"OpenGlassStartMenu.exe";
		}

		bool IsOpenGlassStartMenuAvailable()
		{
			const auto executable = OpenGlassStartMenuExecutable();
			return !executable.empty() && std::filesystem::exists(executable)
				&& std::filesystem::exists(executable.parent_path() / L"OpenGlassStartMenuDLL.dll");
		}

		bool RunClassicStartMenuCommand(const std::wstring& arguments, DWORD* exitCode = nullptr)
		{
			const auto executable = OpenGlassStartMenuExecutable();
			if (!std::filesystem::exists(executable)) return false;
			std::wstring commandLine = L"\"" + executable.wstring() + L"\" " + arguments;
			std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
			command.push_back(L'\0');
			STARTUPINFOW startup{ sizeof(startup) };
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
				return false;
			CloseHandle(process.hThread);
			if (arguments.find(L"-settings") != std::wstring::npos ||
				arguments.find(L"-reloadsettings") != std::wstring::npos ||
				arguments.find(L"-exit") != std::wstring::npos)
			{
				if (exitCode) *exitCode = 0;
				CloseHandle(process.hProcess);
				return true;
			}
			const DWORD waitResult = WaitForSingleObject(process.hProcess, 10000);
			DWORD result = 1;
			GetExitCodeProcess(process.hProcess, &result);
			CloseHandle(process.hProcess);
			if (exitCode) *exitCode = result;
			return result == 0 || (waitResult == WAIT_TIMEOUT && arguments.find(L"-settings") != std::wstring::npos);
		}

		bool StartClassicStartMenu()
		{
			const auto executable = OpenGlassStartMenuExecutable();
			if (!std::filesystem::exists(executable)) return false;
			STARTUPINFOW startup{ sizeof(startup) };
			PROCESS_INFORMATION process{};
			std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
			std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
			command.push_back(L'\0');
			if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
				return false;
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return true;
		}

		bool IsClassicStartMenuRunning()
		{
			wil::unique_handle snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
			if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) return false;
			PROCESSENTRY32W entry{ sizeof(entry) };
			if (!Process32FirstW(snapshot.get(), &entry)) return false;
			do
			{
				if (_wcsicmp(entry.szExeFile, L"OpenGlassStartMenu.exe") == 0) return true;
			} while (Process32NextW(snapshot.get(), &entry));
			return false;
		}

		bool WaitForClassicStartMenu(DWORD timeoutMs)
		{
			const ULONGLONG deadline = GetTickCount64() + timeoutMs;
			do
			{
				if (IsClassicStartMenuRunning()) return true;
				Sleep(50);
			} while (GetTickCount64() < deadline);
			return IsClassicStartMenuRunning();
		}

		std::wstring QuoteArgument(const std::filesystem::path& path)
		{
			return L"\"" + path.wstring() + L"\"";
		}

		bool WriteStringValue(HKEY key, const wchar_t* name, const std::wstring& value)
		{
			return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
		}

		bool WriteDwordValue(HKEY key, const wchar_t* name, DWORD value)
		{
			return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
		}

		bool WriteMultiStringValue(HKEY key, const wchar_t* name, const std::vector<std::wstring>& values)
		{
			std::vector<wchar_t> buffer;
			for (const auto& value : values)
			{
				buffer.insert(buffer.end(), value.begin(), value.end());
				buffer.push_back(L'\0');
			}
			buffer.push_back(L'\0');
			return RegSetValueExW(key, name, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(buffer.size() * sizeof(wchar_t))) == ERROR_SUCCESS;
		}

	}

	void MainFrame::CreateClassicStartMenuTab()
	{
		auto* panel = new wxPanel(m_notebook);
		panel->SetBackgroundColour(wxColour(242, 246, 250));
		auto* root = new wxBoxSizer(wxVERTICAL);

		auto* header = new wxPanel(panel);
		header->SetBackgroundColour(wxColour(35, 55, 66));
		auto* headerSizer = new wxBoxSizer(wxVERTICAL);
		auto* title = new wxStaticText(header, wxID_ANY, L"Меню Пуск и панель задач");
		wxFont titleFont = title->GetFont(); titleFont.SetPointSize(16); titleFont.SetWeight(wxFONTWEIGHT_BOLD); title->SetFont(titleFont); title->SetForegroundColour(*wxWHITE);
		headerSizer->Add(title, 0, wxALL, 12);
		auto* subtitle = new wxStaticText(header, wxID_ANY, L"Встроенное меню Пуск и панель задач Aero");
		subtitle->SetForegroundColour(wxColour(190, 220, 225)); headerSizer->Add(subtitle, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
		header->SetSizer(headerSizer); root->Add(header, 0, wxEXPAND | wxALL, 8);

	auto* status = new wxStaticText(panel, wxID_ANY, L"Изменения сохраняются в профиль Aero Start и применяются без перезапуска окна настроек.");
	status->SetForegroundColour(wxColour(70, 90, 100)); root->Add(status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

	auto* menuBox = new wxStaticBoxSizer(wxVERTICAL, panel, L"Меню Пуск");
	m_chkClassicStartMenuEnabled = new wxCheckBox(panel, wxID_ANY, L"Запускать Aero Start");
	menuBox->Add(m_chkClassicStartMenuEnabled, 0, wxALL, 6);
		m_chkClassicStartButtonEnabled = new wxCheckBox(panel, wxID_ANY, L"Показывать кнопку Пуск");
		menuBox->Add(m_chkClassicStartButtonEnabled, 0, wxALL, 6);
		m_chkClassicSkipMetro = new wxCheckBox(panel, wxID_ANY, L"Пропускать начальный экран Metro");
		menuBox->Add(m_chkClassicSkipMetro, 0, wxALL, 6);
		m_chClassicMenuStyle = new wxChoice(panel, wxID_ANY); m_chClassicMenuStyle->Append(L"Win7"); m_chClassicMenuStyle->Append(L"Classic");
		AddProperty(panel, menuBox, L"Основной стиль:", m_chClassicMenuStyle);
		m_chClassicSkin = new wxChoice(panel, wxID_ANY);
		{
			const std::filesystem::path skinsPath = OpenGlassStartMenuExecutable().parent_path() / L"Skins";
			std::error_code error;
			if (std::filesystem::exists(skinsPath, error))
			{
				for (const auto& entry : std::filesystem::directory_iterator(skinsPath, error))
					if (entry.is_regular_file(error) && entry.path().extension() == L".skin7")
						m_chClassicSkin->Append(entry.path().stem().wstring());
			}
			if (m_chClassicSkin->GetCount() == 0)
			{
				m_chClassicSkin->Append(L"Windows 7");
				m_chClassicSkin->Append(L"Windows Aero");
			}
		}
	AddProperty(panel, menuBox, L"Стиль меню:", m_chClassicSkin);
		m_txtClassicSkinVariation = new wxTextCtrl(panel, wxID_ANY); AddProperty(panel, menuBox, L"Вариант оформления:", m_txtClassicSkinVariation);
	m_chClassicStartButtonType = new wxChoice(panel, wxID_ANY); m_chClassicStartButtonType->Append(L"Системная кнопка"); m_chClassicStartButtonType->Append(L"Своя кнопка");
	AddProperty(panel, menuBox, L"Кнопка Пуск:", m_chClassicStartButtonType);
	m_fpClassicStartButton = new wxFilePickerCtrl(panel, wxID_ANY, wxEmptyString, L"Выберите изображение кнопки Пуск", L"Изображения (*.bmp;*.png)|*.bmp;*.png|Все файлы (*.*)|*.*", wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL);
	AddProperty(panel, menuBox, L"Ассет кнопки:", m_fpClassicStartButton);
	root->Add(menuBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	auto* glassBox = new wxStaticBoxSizer(wxVERTICAL, panel, L"Стекло меню Aero");
	m_chkClassicGlassOverride = new wxCheckBox(panel, wxID_ANY, L"Переопределить цвет стекла"); glassBox->Add(m_chkClassicGlassOverride, 0, wxALL, 6);
	m_cpClassicGlassColor = new wxColourPickerCtrl(panel, wxID_ANY, wxColour(128, 128, 128)); AddProperty(panel, glassBox, L"Цвет стекла:", m_cpClassicGlassColor);
	m_slClassicGlassOpacity = new wxSlider(panel, wxID_ANY, 10, 0, 255); AddProperty(panel, glassBox, L"Прозрачность:", m_slClassicGlassOpacity);
	root->Add(glassBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	auto* taskbarBox = new wxStaticBoxSizer(wxVERTICAL, panel, L"Панель задач");
	m_chkClassicCustomTaskbar = new wxCheckBox(panel, wxID_ANY, L"Использовать оформление Aero"); taskbarBox->Add(m_chkClassicCustomTaskbar, 0, wxALL, 6);
	m_chClassicTaskbarLook = new wxChoice(panel, wxID_ANY); m_chClassicTaskbarLook->Append(L"Непрозрачная"); m_chClassicTaskbarLook->Append(L"Прозрачная"); m_chClassicTaskbarLook->Append(L"Стекло"); AddProperty(panel, taskbarBox, L"Вид панели:", m_chClassicTaskbarLook);
	m_slClassicTaskbarOpacity = new wxSlider(panel, wxID_ANY, 25, 0, 255); AddProperty(panel, taskbarBox, L"Прозрачность панели:", m_slClassicTaskbarOpacity);
	m_fpClassicTaskbarTexture = new wxFilePickerCtrl(panel, wxID_ANY, wxEmptyString, L"Выберите текстуру панели задач", L"Изображения (*.png;*.bmp)|*.png;*.bmp|Все файлы (*.*)|*.*", wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL); AddProperty(panel, taskbarBox, L"Текстура панели:", m_fpClassicTaskbarTexture);
		m_cpClassicTaskbarColor = new wxColourPickerCtrl(panel, wxID_ANY, wxColour(0, 0, 0)); AddProperty(panel, taskbarBox, L"Цвет панели:", m_cpClassicTaskbarColor);
		m_cpClassicTaskbarTextColor = new wxColourPickerCtrl(panel, wxID_ANY, wxColour(255, 255, 255)); AddProperty(panel, taskbarBox, L"Цвет текста панели:", m_cpClassicTaskbarTextColor);
		m_chClassicTaskbarTile = new wxChoice(panel, wxID_ANY); m_chClassicTaskbarTile->Append(L"Tile"); m_chClassicTaskbarTile->Append(L"Stretch"); AddProperty(panel, taskbarBox, L"Режим текстуры:", m_chClassicTaskbarTile);
	root->Add(taskbarBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

		auto* advancedBox = new wxStaticBoxSizer(wxVERTICAL, panel, L"Оформление меню Windows 7");
		advancedBox->Add(new wxStaticText(panel, wxID_ANY, L"Параметры SkinOptionsW7 текущего профиля:"), 0, wxALL, 6);
		const std::pair<const wchar_t*, const wchar_t*> skinOptions[] = {
			{L"USER_IMAGE", L"Показывать изображение пользователя"}, {L"NO_GLARE", L"Отключить блик"}, {L"HALF_GLARE", L"Умеренный блик"}, {L"FULL_GLARE", L"Полный блик"},
			{L"SMALL_ICONS", L"Маленькие значки"}, {L"LARGE_FONT", L"Крупный шрифт"}, {L"GLASS_SHADOW", L"Тень стекла"}, {L"WHITE_SUBMENUS", L"Белые подменю"},
			{L"MAIN_MENU_SHADOW", L"Тень главного меню"}, {L"NO_INTERN", L"Отключить внутреннюю подсветку"}, {L"INTERN1", L"Внутренняя подсветка 1"}, {L"INTERN2", L"Внутренняя подсветка 2"}, {L"INTERN3", L"Внутренняя подсветка 3"},
			{L"SHUTDOWN_UPDATE_IMAGE", L"Изображение завершения работы"}, {L"SHUTDOWN_UPDATE_BLUE_IMAGE", L"Синее изображение завершения"}, {L"LOW_MILD", L"Мягкая низкая подсветка"}
		};
		auto* optionsGrid = new wxFlexGridSizer(2, 4, 4);
		for (const auto& [key, label] : skinOptions)
		{
			auto* check = new wxCheckBox(panel, wxID_ANY, label);
			m_classicSkinOptionChecks.emplace_back(key, check);
			optionsGrid->Add(check, 0, wxEXPAND | wxALL, 3);
		}
		advancedBox->Add(optionsGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
		m_txtClassicSkinOptions = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 46), wxTE_MULTILINE | wxTE_READONLY);
		m_txtClassicSkinOptions->SetToolTip(L"Исходные строки SkinOptionsW7 для диагностики");
		advancedBox->Add(m_txtClassicSkinOptions, 0, wxEXPAND | wxALL, 6);
		root->Add(advancedBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	auto* fullProfileBox = new wxStaticBoxSizer(wxVERTICAL, panel, L"Полный профиль меню Aero Start");
		fullProfileBox->Add(new wxStaticText(panel, wxID_ANY,
			L"Все параметры встроенного меню сохраняются в XML-профиле, включая расширенные настройки меню, поиска, поведения, звуков и пользовательских пунктов."),
			0, wxALL, 6);
		auto* profileButtons = new wxBoxSizer(wxHORIZONTAL);
		auto* openProfile = new wxButton(panel, wxID_ANY, L"Открыть все параметры");
		auto* exportProfile = new wxButton(panel, wxID_ANY, L"Экспорт XML");
		auto* importProfile = new wxButton(panel, wxID_ANY, L"Импорт XML");
		auto* reloadProfile = new wxButton(panel, wxID_ANY, L"Применить профиль");
		profileButtons->Add(openProfile, 0, wxRIGHT, 5);
		profileButtons->Add(exportProfile, 0, wxRIGHT, 5);
		profileButtons->Add(importProfile, 0, wxRIGHT, 5);
		profileButtons->Add(reloadProfile, 0);
		fullProfileBox->Add(profileButtons, 0, wxALL, 6);
		root->Add(fullProfileBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
		openProfile->SetLabel(L"Показать все параметры здесь");
		openProfile->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowEmbeddedClassicSettings(); });
		exportProfile->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ExportClassicShellSettings(); });
		importProfile->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ImportClassicShellSettings(); });
		reloadProfile->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReloadClassicShell(); });

		m_classicEmbeddedHost = new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 760));
		m_classicEmbeddedHost->SetBackgroundColour(wxColour(242, 246, 250));
		root->Add(m_classicEmbeddedHost, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
		m_classicEmbeddedHost->Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
		{
			ResizeEmbeddedClassicSettings();
			event.Skip();
		});
		// The full native settings surface is the page itself. The former
		// compact AeroGlass controls remain only as compatibility code for
		// profile loading/export and are deliberately removed from the layout.
		root->Hide(header);
		root->Hide(status);
		root->Hide(menuBox);
		root->Hide(glassBox);
		root->Hide(taskbarBox);
		root->Hide(advancedBox);
		root->Hide(fullProfileBox);
		m_classicEmbeddedPollTimer.SetOwner(this);
		Bind(wxEVT_TIMER, &MainFrame::PollEmbeddedClassicSettings, this, m_classicEmbeddedPollTimer.GetId());

	auto mark = [this](wxWindow* control) { control->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e){ SetDirty(true); e.Skip(); }); control->Bind(wxEVT_COMMAND_CHOICE_SELECTED, [this](wxCommandEvent& e){ SetDirty(true); e.Skip(); }); control->Bind(wxEVT_COMMAND_SLIDER_UPDATED, [this](wxCommandEvent& e){ SetDirty(true); e.Skip(); }); control->Bind(wxEVT_FILEPICKER_CHANGED, [this](wxFileDirPickerEvent& e){ SetDirty(true); e.Skip(); }); };
		mark(m_chkClassicStartMenuEnabled); mark(m_chkClassicStartButtonEnabled); mark(m_chkClassicSkipMetro); mark(m_chClassicMenuStyle); mark(m_chClassicSkin); mark(m_txtClassicSkinVariation); mark(m_chClassicStartButtonType); mark(m_fpClassicStartButton); mark(m_chkClassicGlassOverride); mark(m_slClassicGlassOpacity); mark(m_chkClassicCustomTaskbar); mark(m_chClassicTaskbarLook); mark(m_slClassicTaskbarOpacity); mark(m_fpClassicTaskbarTexture); mark(m_chClassicTaskbarTile);
		for (const auto& option : m_classicSkinOptionChecks) mark(option.second);
	m_cpClassicGlassColor->Bind(wxEVT_COLOURPICKER_CHANGED, [this](wxColourPickerEvent& e){ SetDirty(true); e.Skip(); });
		m_cpClassicTaskbarColor->Bind(wxEVT_COLOURPICKER_CHANGED, [this](wxColourPickerEvent& e){ SetDirty(true); e.Skip(); });
		m_cpClassicTaskbarTextColor->Bind(wxEVT_COLOURPICKER_CHANGED, [this](wxColourPickerEvent& e){ SetDirty(true); e.Skip(); });

		panel->SetSizer(root); m_notebook->AddPage(panel, L"Меню Пуск");
		CallAfter([this]()
		{
			ShowEmbeddedClassicSettings();
			ResizeEmbeddedClassicSettings();
		});
	}

	void MainFrame::LoadClassicStartMenuSettings()
	{
		if (!m_chClassicSkin) return;
		EnsureDefaultStartButtonAsset();
		m_chkClassicStartMenuEnabled->SetValue(true);
		m_chkClassicStartButtonEnabled->SetValue(ReadDwordValue(L"EnableStartButton", 1) != 0);
		m_chkClassicSkipMetro->SetValue(ReadDwordValue(L"SkipMetro", 1) != 0);
		const DWORD menuStyle = static_cast<DWORD>(ReadIntOrRadioValue(L"MenuStyle", 2, { L"Classic1", L"Classic2", L"Win7" }));
		m_chClassicMenuStyle->SetSelection(menuStyle == 2 ? 0 : 1);
		const auto skin = ReadStringValue(L"SkinW7");
		const int skinSelection = m_chClassicSkin->FindString(skin);
		m_chClassicSkin->SetSelection(skinSelection == wxNOT_FOUND ? 0 : skinSelection);
		m_txtClassicSkinVariation->SetValue(ReadStringValue(L"SkinVariationW7"));
		const auto skinOptions = ReadMultiStringValue(L"SkinOptionsW7");
		m_txtClassicSkinOptions->SetValue(skinOptions);
		for (auto& option : m_classicSkinOptionChecks) option.second->SetValue(false);
		std::wistringstream optionStream(skinOptions);
		std::wstring optionLine;
		while (std::getline(optionStream, optionLine))
		{
			const auto separator = optionLine.find(L'=');
			if (separator == std::wstring::npos) continue;
			const auto name = optionLine.substr(0, separator);
			const bool enabled = optionLine.substr(separator + 1) == L"1";
			for (auto& option : m_classicSkinOptionChecks)
				if (option.first == name) option.second->SetValue(enabled);
		}
		m_chClassicStartButtonType->SetSelection(ReadIntOrRadioValue(L"StartButtonType", 0, { L"AeroButton", L"ClasicButton", L"CustomButton" }) == 2 ? 1 : 0);
		m_fpClassicStartButton->SetPath(ReadStringValue(L"StartButtonPath"));
		m_chkClassicGlassOverride->SetValue(ReadDwordValue(L"GlassOverride", 0) != 0);
		const DWORD color = ReadDwordValue(L"GlassColor", 0x808080); m_cpClassicGlassColor->SetColour(wxColour((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff));
		m_slClassicGlassOpacity->SetValue(std::clamp<int>(ReadDwordValue(L"GlassOpacity", 10), 0, 255));
		m_chkClassicCustomTaskbar->SetValue(ReadDwordValue(L"CustomTaskbar", 0) != 0);
		m_chClassicTaskbarLook->SetSelection(std::clamp(ReadIntOrRadioValue(L"TaskbarLook", 1, { L"Opaque", L"Transparent", L"Glass" }), 0, 2));
		m_slClassicTaskbarOpacity->SetValue(std::clamp<int>(ReadDwordValue(L"TaskbarOpacity", 25), 0, 255));
		m_fpClassicTaskbarTexture->SetPath(ReadStringValue(L"TaskbarTexture"));
		const DWORD taskbarColor = ReadDwordValue(L"TaskbarColor", 0); m_cpClassicTaskbarColor->SetColour(wxColour((taskbarColor >> 16) & 0xff, (taskbarColor >> 8) & 0xff, taskbarColor & 0xff));
		const DWORD taskbarTextColor = ReadDwordValue(L"TaskbarTextColor", 0xffffff); m_cpClassicTaskbarTextColor->SetColour(wxColour((taskbarTextColor >> 16) & 0xff, (taskbarTextColor >> 8) & 0xff, taskbarTextColor & 0xff));
		m_chClassicTaskbarTile->SetSelection(std::clamp(ReadIntOrRadioValue(L"TaskbarTileH", 1, { L"Tile", L"Stretch" }), 0, 1));

		// The setting is enabled by default, but loading the profile used to only
		// tick the checkbox. If Aero Start had exited, launching Aero Glass again
		// left Explorer without our Start Menu component until the user pressed
		// Save. Restore the runtime asynchronously after the frame is initialized.
		if (m_classicSettingsModule && m_createEmbeddedSettings && m_chkClassicStartMenuEnabled->IsChecked())
		{
			CallAfter([this]()
			{
				if (!IsOpenGlassStartMenuAvailable() || IsClassicStartMenuRunning()) return;
				if (StartClassicStartMenu() && WaitForClassicStartMenu(3000))
					RunClassicStartMenuCommand(L"-reloadsettings");
			});
		}
	}

	bool MainFrame::SaveClassicStartMenuSettings()
	{
		if (!m_chClassicSkin) return true;
		// The full Open-Shell settings surface is embedded in this page and
		// writes the complete catalog directly to the shared registry key. The
		// compact controls below are compatibility-only and may contain stale
		// values after the user edits the embedded dialog. Never overwrite the
		// embedded result with those hidden controls.
		if (m_classicSettingsModule && m_createEmbeddedSettings)
		{
			if (m_chkClassicStartMenuEnabled->IsChecked() && !IsOpenGlassStartMenuAvailable())
			{
				wxMessageBox(L"Встроенный компонент меню Aero Start ещё не собран и не может быть запущен.", L"Aero", wxOK | wxICON_WARNING, this);
				return false;
			}
			if (m_chkClassicStartMenuEnabled->IsChecked())
			{
				if (!IsClassicStartMenuRunning() && !StartClassicStartMenu())
				{
					wxMessageBox(L"Не удалось запустить встроенное меню Aero Start.", L"Aero", wxOK | wxICON_ERROR, this);
					return false;
				}
				if (!WaitForClassicStartMenu(3000) || !RunClassicStartMenuCommand(L"-reloadsettings"))
				{
					wxMessageBox(L"Меню Aero Start запущено, но не подтвердило применение настроек.", L"Aero", wxOK | wxICON_WARNING, this);
					return false;
				}
			}
			else
			{
				RunClassicStartMenuCommand(L"-exit");
			}
			return true;
		}
		wil::unique_hkey key;
		DWORD disposition = 0;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kClassicSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.put(), &disposition) != ERROR_SUCCESS) return false;
		std::vector<std::wstring> skinOptions;
		for (const auto& option : m_classicSkinOptionChecks)
			skinOptions.emplace_back(option.first + (option.second->IsChecked() ? L"=1" : L"=0"));
		const bool ok = WriteIntOrRadioValue(key.get(), L"MenuStyle", m_chClassicMenuStyle->GetSelection() == 0 ? 2 : 0, { L"Classic1", L"Classic2", L"Win7" })
			&& WriteDwordValue(key.get(), L"EnableStartButton", m_chkClassicStartButtonEnabled->IsChecked() ? 1 : 0)
			&& WriteDwordValue(key.get(), L"SkipMetro", m_chkClassicSkipMetro->IsChecked() ? 1 : 0)
			&& WriteStringValue(key.get(), L"SkinW7", m_chClassicSkin->GetString(std::clamp(m_chClassicSkin->GetSelection(), 0, static_cast<int>(m_chClassicSkin->GetCount()) - 1)).ToStdWstring())
			&& WriteStringValue(key.get(), L"SkinVariationW7", m_txtClassicSkinVariation->GetValue().ToStdWstring())
			&& WriteIntOrRadioValue(key.get(), L"StartButtonType", m_chClassicStartButtonType->GetSelection() == 1 ? 2 : 0, { L"AeroButton", L"ClasicButton", L"CustomButton" })
			&& WriteStringValue(key.get(), L"StartButtonPath", m_fpClassicStartButton->GetPath().ToStdWstring())
			&& WriteDwordValue(key.get(), L"GlassOverride", m_chkClassicGlassOverride->IsChecked() ? 1 : 0)
			&& WriteDwordValue(key.get(), L"GlassColor", (m_cpClassicGlassColor->GetColour().Red() << 16) | (m_cpClassicGlassColor->GetColour().Green() << 8) | m_cpClassicGlassColor->GetColour().Blue())
			&& WriteDwordValue(key.get(), L"GlassOpacity", static_cast<DWORD>(m_slClassicGlassOpacity->GetValue()))
			&& WriteDwordValue(key.get(), L"CustomTaskbar", m_chkClassicCustomTaskbar->IsChecked() ? 1 : 0)
			&& WriteIntOrRadioValue(key.get(), L"TaskbarLook", std::clamp(m_chClassicTaskbarLook->GetSelection(), 0, 2), { L"Opaque", L"Transparent", L"Glass" })
			&& WriteDwordValue(key.get(), L"TaskbarOpacity", static_cast<DWORD>(m_slClassicTaskbarOpacity->GetValue()))
			&& WriteStringValue(key.get(), L"TaskbarTexture", m_fpClassicTaskbarTexture->GetPath().ToStdWstring())
			&& WriteDwordValue(key.get(), L"TaskbarColor", (m_cpClassicTaskbarColor->GetColour().Red() << 16) | (m_cpClassicTaskbarColor->GetColour().Green() << 8) | m_cpClassicTaskbarColor->GetColour().Blue())
			&& WriteDwordValue(key.get(), L"TaskbarTextColor", (m_cpClassicTaskbarTextColor->GetColour().Red() << 16) | (m_cpClassicTaskbarTextColor->GetColour().Green() << 8) | m_cpClassicTaskbarTextColor->GetColour().Blue())
			&& WriteIntOrRadioValue(key.get(), L"TaskbarTileH", std::clamp(m_chClassicTaskbarTile->GetSelection(), 0, 1), { L"Tile", L"Stretch" })
			&& WriteMultiStringValue(key.get(), L"SkinOptionsW7", skinOptions);
		if (!ok) wxMessageBox(L"Не удалось сохранить настройки Classic Start Menu.", L"Classic Start Menu", wxOK | wxICON_ERROR, this);
		else
		{
			if (m_chkClassicStartMenuEnabled->IsChecked() && !IsOpenGlassStartMenuAvailable())
			{
				wxMessageBox(L"Встроенный компонент меню Aero Start ещё не собран и не может быть запущен. Настройки сохранены.", L"Aero", wxOK | wxICON_WARNING, this);
				return false;
			}
			RunClassicStartMenuCommand(L"-reloadsettings");
			if (m_chkClassicStartMenuEnabled->IsChecked())
			{
				if (!IsClassicStartMenuRunning()) StartClassicStartMenu();
			}
			else
			{
				RunClassicStartMenuCommand(L"-exit");
			}
		}
		return ok;
	}

	void MainFrame::ResizeEmbeddedClassicSettings()
	{
		if (!m_classicEmbeddedSettings || !m_classicEmbeddedHost) return;
		wxSize size = m_classicEmbeddedHost->GetClientSize();
		if (size.x <= 1 || size.y <= 1)
		{
			const wxSize parentSize = m_classicEmbeddedHost->GetParent() ? m_classicEmbeddedHost->GetParent()->GetClientSize() : wxSize(1, 1);
			size = wxSize(std::max(1, parentSize.x - 16), std::max(1, parentSize.y - 16));
			m_classicEmbeddedHost->SetSize(size);
		}
		SetWindowPos(m_classicEmbeddedSettings, HWND_TOP, 0, 0, std::max(1, size.x), std::max(1, size.y), SWP_NOACTIVATE | SWP_SHOWWINDOW);
		RedrawWindow(m_classicEmbeddedSettings, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
		RedrawWindow(static_cast<HWND>(m_classicEmbeddedHost->GetHandle()), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
		UpdateWindow(m_classicEmbeddedSettings);
	}

	void MainFrame::ShowEmbeddedClassicSettings()
	{
		if (!m_classicEmbeddedHost) return;
		if (!m_classicSettingsModule)
		{
			const auto dll = OpenGlassStartMenuExecutable().parent_path() / L"OpenGlassStartMenuDLL.dll";
			m_classicSettingsModule = LoadLibraryW(dll.c_str());
			if (m_classicSettingsModule)
			{
				m_createEmbeddedSettings = reinterpret_cast<CreateEmbeddedSettingsFn>(GetProcAddress(m_classicSettingsModule, "CreateEmbeddedSettings"));
				m_closeEmbeddedSettings = reinterpret_cast<CloseEmbeddedSettingsFn>(GetProcAddress(m_classicSettingsModule, "CloseEmbeddedSettings"));
				m_consumeEmbeddedSettingsApplied = reinterpret_cast<ConsumeEmbeddedSettingsAppliedFn>(GetProcAddress(m_classicSettingsModule, "ConsumeEmbeddedSettingsApplied"));
			}
		}
		if (!m_createEmbeddedSettings)
		{
			wxMessageBox(L"Не удалось загрузить встроенные полные настройки меню AeroGlass.", L"AeroGlass", wxOK | wxICON_ERROR, this);
			return;
		}
		m_classicEmbeddedHost->Show();
		m_classicEmbeddedHost->GetParent()->Layout();
		if (!m_classicEmbeddedSettings)
			m_classicEmbeddedSettings = m_createEmbeddedSettings(static_cast<HWND>(m_classicEmbeddedHost->GetHandle()), 0);
		if (!m_classicEmbeddedSettings)
		{
			wxMessageBox(L"Не удалось создать встроенное окно полных настроек меню AeroGlass.", L"AeroGlass", wxOK | wxICON_ERROR, this);
			return;
		}
		ResizeEmbeddedClassicSettings();
		m_classicEmbeddedHost->GetParent()->Layout();
		ResizeEmbeddedClassicSettings();
		m_classicEmbeddedPollTimer.Start(250);
		if (m_chkClassicStartMenuEnabled && m_chkClassicStartMenuEnabled->IsChecked() && !IsClassicStartMenuRunning())
		{
			if (StartClassicStartMenu() && WaitForClassicStartMenu(3000))
				RunClassicStartMenuCommand(L"-reloadsettings");
		}
	}

	void MainFrame::PollEmbeddedClassicSettings(wxTimerEvent& event)
	{
		event.Skip();
		if (!m_classicEmbeddedSettings)
		{
			m_classicEmbeddedPollTimer.Stop();
			return;
		}
		if (m_consumeEmbeddedSettingsApplied && m_consumeEmbeddedSettingsApplied())
			RunClassicStartMenuCommand(L"-reloadsettings");
		if (IsWindow(m_classicEmbeddedSettings)) return;

		m_classicEmbeddedSettings = nullptr;
		m_classicEmbeddedPollTimer.Stop();
		// The native Open-Shell settings dialog writes the registry itself. Its
		// close callback is inside the embedded DLL, so explicitly notify the
		// already-running Explorer hook after the child has accepted the changes.
		RunClassicStartMenuCommand(L"-reloadsettings");
		// OK/Cancel closes the native settings instance. The host page must not
		// become empty after that operation: recreate the embedded view in the
		// same page after Explorer has received the reload request.
		ShowEmbeddedClassicSettings();
	}

	void MainFrame::OpenClassicShellSettings()
	{
		if (!RunClassicStartMenuCommand(L"-settings"))
			wxMessageBox(L"Не удалось открыть полные настройки меню Aero Start.", L"Aero", wxOK | wxICON_ERROR, this);
	}

	void MainFrame::ExportClassicShellSettings()
	{
		wxFileDialog dialog(this, L"Экспорт полного профиля меню Aero Start", wxEmptyString, L"Aero-StartMenu-settings.xml", L"XML (*.xml)|*.xml", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() != wxID_OK) return;
		DWORD exitCode{};
		if (!RunClassicStartMenuCommand(L"-backup " + QuoteArgument(dialog.GetPath().ToStdWstring()), &exitCode))
			wxMessageBox(wxString::Format(L"Не удалось экспортировать профиль (код %lu).", exitCode), L"Aero", wxOK | wxICON_ERROR, this);
	}

	void MainFrame::ImportClassicShellSettings()
	{
		wxFileDialog dialog(this, L"Импорт полного профиля меню Aero Start", wxEmptyString, wxEmptyString, L"XML (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dialog.ShowModal() != wxID_OK) return;
		DWORD exitCode{};
		if (!RunClassicStartMenuCommand(L"-xml " + QuoteArgument(dialog.GetPath().ToStdWstring()), &exitCode))
		{
			wxMessageBox(wxString::Format(L"Не удалось импортировать профиль (код %lu).", exitCode), L"Aero", wxOK | wxICON_ERROR, this);
			return;
		}
		ReloadClassicShell();
		LoadClassicStartMenuSettings();
		SetDirty(true);
	}

	void MainFrame::ReloadClassicShell()
	{
		if (!RunClassicStartMenuCommand(L"-reloadsettings"))
			wxMessageBox(L"Не удалось отправить встроенному меню команду обновления.", L"Aero", wxOK | wxICON_ERROR, this);
	}
}
