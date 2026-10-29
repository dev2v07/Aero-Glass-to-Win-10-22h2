# OpenGlass Start Menu

Это встроенный компонент OpenGlass для меню «Пуск» и панели задач. Он основан на исходниках Open-Shell, которые распространяются по MIT License; установленный Classic Shell не используется.

Компонент должен поставляться вместе с OpenGlass как:

- `OpenGlassStartMenu.exe` — процесс меню;
- `OpenGlassStartMenuDLL.dll` — собственная DLL меню;
- собственные ресурсы скинов и локализации.

Настройки хранятся в `HKCU\Software\OpenGlass\StartMenu\Settings`. В исходниках сохранён однократный путь миграции старых данных Classic Shell, но после миграции runtime работает только с OpenGlass-профилем.

## Текущее состояние

- исходники и лицензия перенесены в репозиторий;
- внутренние имена DLL и registry path переведены на OpenGlass;
- GUI больше не обращается к `C:\Program Files\Classic Shell`;
- все 16 resource-only skin projects собираются отдельным этапом в `build\bin\Skins`;
- полная сборка выполняется скриптом `Build-OpenGlassStartMenu.ps1` и пока ожидает ATL/MFC workload Visual Studio для `atlbase.h`.

Пример сборки из корня репозитория:

```powershell
OpenGlassStartMenu\Build-OpenGlassStartMenu.ps1 -Configuration Release -Platform x64 -PlatformToolset v143 -VcpkgInstalledDir 'C:\projects\Aero Glass\installed-ascii'
```
