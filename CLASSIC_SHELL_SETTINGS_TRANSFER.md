# Перенос меню Classic Shell / Open-Shell в OpenGlass

Текущий целевой runtime — встроенный `OpenGlassStartMenu.exe`, а не установленный Classic Shell. Open-Shell используется как исходная база для переноса меню, настроек и скинов; после сборки компонент поставляется внутри OpenGlass и не требует установки сторонней программы.

## Канонический источник

Проверен установленный Classic Shell 4.3.1 и исходники Open-Shell. Каталог параметров находится в `Src/StartMenu/StartMenuDLL/SettingsUI.cpp`; это не ограниченный набор параметров из реестра, а полный каталог настроек компонента StartMenu.

Рабочие reference-источники проекта: [Open-Shell](https://github.com/Open-Shell/Open-Shell-Menu), [OpenGlass](https://github.com/ALTaleX531/OpenGlass) и исторический сайт [Classic Shell](https://classicshell.net/). Open-Shell используется для актуального поведения меню и формата настроек, OpenGlass — для DWM-стека, сайт Classic Shell — для совместимости с версией 4.3.1 и исходными скинами.

## Как обеспечивается полный перенос

В GUI добавлен блок полного профиля встроенного меню с четырьмя операциями:

- открыть полный редактор Classic Shell (`ClassicStartMenu.exe -settings`);
- экспортировать весь профиль (`-backup file.xml`);
- импортировать весь профиль (`-xml file.xml`);
- применить настройки через штатный `-reloadsettings`.

XML-операции передают встроенному компоненту весь каталог параметров, включая пункты меню, поиск, поведение, размеры и анимацию, Windows Menu, контекстное меню, звуки, язык, пользовательские меню Classic 1/2/7 и скрытые совместимые параметры.

## Параметры, подключенные к компактной панели

Подключены с корректными типами Classic Shell:

- `MenuStyle`, `EnableStartButton`, `SkipMetro`;
- `SkinW7`, `SkinVariationW7`, `SkinOptionsW7`;
- `StartButtonType`, `StartButtonPath`;
- `GlassOverride`, `GlassColor`, `GlassOpacity`;
- `CustomTaskbar`, `TaskbarLook`, `TaskbarOpacity`, `TaskbarColor`, `TaskbarTextColor`, `TaskbarTexture`, `TaskbarTileH`.

`DWORD`, строковые и `REG_MULTI_SZ` значения записываются именно в типах, которые использует Classic Shell. После сохранения выполняется штатная перезагрузка настроек; запуск/выход Classic Start Menu выполняется только по флажку пользователя.

## Скины и язык

В проект перенесены все 17 файлов из установленной папки `Classic Shell\\Skins`, включая `.skin` и `.skin7`, а также `ru-RU.dll`. Они являются исходными ресурсами для встроенного компонента и не требуют установленного Classic Shell.

`ClassicStartMenuDLL.dll` больше не используется как внешний runtime-компонент. В составе проекта собирается собственная переименованная DLL встроенного меню.

## Граница текущей проверки

Подтверждено командой `ClassicStartMenu.exe -backup`: штатный XML-профиль создаётся с кодом 0 и содержит текущие значения профиля. Служба OpenGlass и DWM-инъекция во время этой проверки не запускались. Реальное изменение оболочки будет проверяться отдельно после восстановления корректной пары Host + архитектурная DLL и только с резервной копией настроек.
