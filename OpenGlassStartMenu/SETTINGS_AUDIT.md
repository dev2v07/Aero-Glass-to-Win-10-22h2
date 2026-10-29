# Аудит настроек OpenGlass Start Menu

Дата: 2026-10-28

Источник: `StartMenu/StartMenuDLL/SettingsUI.cpp` внутреннего компонента Open-Shell.

## Результат инвентаризации

- 374 записей `CSetting` обнаружено в каталоге настроек.
- 22 записи являются группами/вкладками.
- 352 записи являются параметрами, радиокнопками или служебными значениями.
- Группы включают стили меню, Basic, Controls, SpecialItems, MainMenu, GeneralBehavior, SearchBoxSettings, Look, Skin, StartButton, Taskbar, Metro, CustomMenu, WindowsMenu, AllProgramsSkin, ContextMenu, Sounds и Language.

## Что сейчас связано с основным GUI OpenGlass

Основной GUI напрямую записывает только базовый набор профиля меню: стиль, кнопку Пуск, Metro, skin Win7, variation, тип и путь кнопки, стекло меню, taskbar look/opacity/texture/color/text color/tile и `SkinOptionsW7`.

Остальные параметры пока доступны через штатное окно подробных настроек внутреннего `OpenGlassStartMenu.exe` по команде `-settings`, но не представлены отдельными контролами основного окна OpenGlass.

## Вывод

Перенос всех параметров ещё не завершён. Следующий функциональный этап после сборки ATL/MFC — построить типизированный каталог параметров и перенести все 352 non-group записи в единый GUI, сохранив исходные типы, диапазоны, radio-значения, скрытые/служебные флаги и совместимость XML-профилей.
