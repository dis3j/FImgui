# Примеры Fimgui

Всё, что использовалось в демонстрациях: исходники дизайнов, готовые экспортированные C++-проекты, тестовые медиа и скрипты, которыми они созданы и проверены.

## Дизайны (открываются в Fimgui)

| Файл | Что внутри |
|---|---|
| `demo_menu.fimgui` | «Nexus» — тёмное игровое меню: сайдбар с иконками, 4 вкладки, тумблеры, слайдеры, выбор кости, поле ввода, кнопки |
| `glass_demo.fimgui` | «Aurora» — витрина эффектов: liquid glass, frosted-сайдбар (background blur), зерно (texture overlay), анимации появления, hover и петли |
| `media_demo.fimgui` | Анимированный GIF как тайловая текстура с прокруткой |
| `combo_demo.fimgui` | Combo (выбор кости) и Multi-combo (цели ESP, «Everything» / «Nothing») |
| `keyframes_demo.fimgui` | Ключевые кадры: мяч прыгает по дуге в цикле (X, Y, Scale), заголовок выезжает и проявляется. Откройте таймлайн: Alt+T |

Рядом лежат `*.jsonl` — те же дизайны в виде потока операций, в том формате, в котором отвечает ИИ (встроенный чат и MCP `apply_operations`):

```
Fimgui.exe --ops examples\glass_demo.jsonl
```

## Экспорт в Minecraft

`export/minecraft_nexus` — «Nexus», экспортированный для **Fabric 1.21.1** (Export → Minecraft Java): `src/main/java/com/example/examplemod/client/gui/` (`NexusMenu`, `NexusScreen`, runtime `fimgui/`) и `src/main/resources/assets/examplemod/` (запечённые слои и шрифты Inter). Скопируйте `src/` в свой мод и откройте `new NexusScreen()`.

## Экспортированные C++-проекты

`export/nexus_menu` и `export/aurora_glass` — результат кнопки Export: `menu/` (код меню, runtime, эффекты), `icons/`, `images/`, `fonts/`, `example/` (хост Win32 + DirectX 11). `IMGUI_DIR` уже указывает на `third_party/imgui` этого репозитория.

Сборка и запуск (шрифты ищутся в `../fonts` относительно рабочей папки, поэтому запускайте из `example`):

```bat
cd examples\export\aurora_glass\example
cmake -S . -B build
cmake --build build --config Release
build\Release\fimgui_example.exe
```

## Медиа

- `media/stripes.gif` — анимированный GIF, сгенерированный `scripts/make_gif.py`.
- Видео в репозиторий не положено. Для проверки видео возьмите любой свой MP4:
  ```
  Fimgui.exe --import путь\к\видео.mp4 0 0
  ```
  или перетащите файл на холст. Как текстуру видео выбирают в Effects → Texture overlay.

## Скрипты (`scripts/`)

| Скрипт | Назначение |
|---|---|
| `gen_demo.py out.jsonl` | Генерирует операции меню «Nexus» |
| `gen_glass.py out.jsonl` | Генерирует операции витрины «Aurora» |
| `make_gif.py out.gif` | Пишет анимированный GIF без сторонних библиотек |
| `mock_server.py ops.jsonl log.json [port]` | Локальная имитация Claude API (SSE-стрим), чтобы проверить встроенный чат без ключа: `Fimgui.exe --ai-base http://127.0.0.1:8765 --ai-key test --ai "..."` |
| `mcp_test.py port ops.jsonl out.png fimgui-mcp.exe` | Проверка MCP-сервера: HTTP-клиент и stdio-мост (initialize, tools/list, apply_operations, render_image и т.д.). Запустите Fimgui с `--mcp-port port` |
