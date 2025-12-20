# CAN LED status для TTGO T-CAN48

Пример прошивки для LilyGO® TTGO T-CAN48 (ESP32 + CAN) для отображения статусов автомобиля на адресной ленте WS2812/Neopixel. Лента меняет вид в зависимости от команд, приходящих по CAN.

## Возможности
- Скорость шины 1 Мбит/с (TWAI/CAN драйвер ESP32).
- Отрисовка состояния педали газа, тормоза, ручника, оборотов, температуры двигателя и отсечки.
- Настраиваемые пины CAN и пин ленты.
- Простой протокол кадров — см. [`docs/CAN_PROTOCOL.md`](docs/CAN_PROTOCOL.md).

## Подключение
Схема подключения и советы приведены в [`docs/CONNECTIONS.md`](docs/CONNECTIONS.md). Кратко:
- CANH/CANL — к шине автомобиля, GND — к массе.
- DIN ленты — к `GPIO4`, питание ленты 5 В.
- Логи в Serial на 115200 бод.

## Сборка и прошивка
Используется PlatformIO.

```bash
# Установить зависимости и собрать
pio run

# Залить прошивку на плату (указать нужный порт)
pio run --target upload --upload-port /dev/ttyUSB0

# Открыть сериальный монитор
pio device monitor -b 115200
```

## Настройка логики
Ключевые параметры находятся в [`src/main.cpp`](src/main.cpp):
- `CAN_TX_PIN`, `CAN_RX_PIN` — пины CAN-трансивера.
- `LED_PIN`, `LED_COUNT`, `LED_BRIGHTNESS` — параметры ленты.
- `ID_*` — идентификаторы кадров протокола.
- `rpmRedline` в структуре `VehicleState` — точка срабатывания красной зоны.

Для изменения визуализаций редактируйте функции `drawThrottleBar`, `drawRpmGradient`, `drawCoolantIndicator`, `applyBrakeOverlays`, `drawRevLimiter`.
