# Link ECU Integration Guide

## Обзор

Прошивка поддерживает три CAN протокола:
- **Custom Protocol** (CAN_PROTOCOL = 0) - Оригинальный пользовательский протокол
- **Link ECU Generic Dashboard** (CAN_PROTOCOL = 1) - Совместимость с Link Fury X и другими ECU Link
- **Link ECU Generic Dashboard 2** (CAN_PROTOCOL = 2) - Новый расширенный протокол Link ECU

## Быстрая настройка

### Для Link Fury X (Generic Dashboard)

1. Откройте `src/config.h`
2. Установите протокол:
   ```cpp
   #define CAN_PROTOCOL 1
   ```
3. Скомпилируйте и прошейте:
   ```bash
   pio run --target upload
   ```

### Для Link ECU с Generic Dashboard 2

1. Откройте `src/config.h`
2. Установите протокол:
   ```cpp
   #define CAN_PROTOCOL 2
   ```
3. Скомпилируйте и прошейте

## Настройка Link ECU

### PCLink / Link G4+

1. Откройте PCLink и подключитесь к ECU
2. Перейдите в **CAN Setup** → **Generic Dash**
3. Включите **Generic Dash CAN Stream**
4. Установите скорость CAN: **1 Mbps (1000 kbps)**
5. Выберите протокол:
   - **Generic Dashboard** - для старых ECU
   - **Generic Dashboard 2** - для новых ECU (рекомендуется)
6. Сохраните настройки в ECU

### Link G4X / Fury X

1. Откройте последнюю версию PCLink
2. В разделе **CAN** → **CAN Bus Setup**
3. Выберите **CAN1** или **CAN2** (в зависимости от вашего подключения)
4. Установите **Baud Rate: 1000 kbps**
5. Включите **Generic Dash Stream**
6. Выберите **Protocol: Generic Dashboard 2**
7. Запишите конфигурацию в ECU

## Поддерживаемые параметры

### Generic Dashboard (Protocol 1)

| Параметр | CAN ID | Описание | Формат |
|----------|--------|----------|--------|
| RPM & TPS | 0x5F0 | Обороты двигателя и положение дросселя | RPM: uint32, TPS: uint16 (×0.1%) |
| Fuel & Timing | 0x5F1 | Давление топлива и угол зажигания | FuelP: uint16 (×0.1 bar), Timing: int16 (×0.1°) |
| Pressures | 0x5F2 | Давления (MAP, Baro) и Lambda | Lambda: uint16 (×0.01) |
| Temperatures | 0x5F3 | Температуры охл. жидкости и воздуха | ECT/IAT: uint16 (×0.1°C) |
| Voltage & Flags | 0x5F4 | Напряжение АКБ и флаги состояния | Voltage: uint16 (×0.01V) |
| Gear & Oil | 0x5F5 | Передача и давление масла | Gear: uint8, Oil: uint16 (×0.1 bar) |
| Speed | 0x5F6 | Скорость автомобиля | Speed: uint16 (×0.1 km/h) |

### Generic Dashboard 2 (Protocol 2)

| Параметр | CAN ID | Описание | Данные |
|----------|--------|----------|--------|
| Engine Data 1 | 0x2000 | RPM, TPS, ECT, IAT | 8 байт основных параметров двигателя |
| Engine Data 2 | 0x2001 | MAP, Battery, Fuel P, Oil P | 8 байт давлений и напряжения |
| Engine Data 3 | 0x2002 | Lambda, Timing, Fuel level | 8 байт параметров смеси |
| Engine Data 4 | 0x2003 | Boost, Idle control | 8 байт управления |
| Vehicle Data 1 | 0x2004 | Speed, Gear, Launch/Flat shift | 8 байт данных автомобиля |
| Vehicle Data 2 | 0x2005 | Wheel speeds | 8 байт скоростей колёс |
| Flags & Warnings | 0x2006 | Engine protection, Warnings | Флаги защиты двигателя |
| Analog Inputs | 0x2007 | User-configurable inputs | Пользовательские входы |

## Визуализация данных

### Основные параметры на LED ленте:

- **RPM** - Градиент от синего (низкие обороты) до жёлтого (около отсечки)
- **TPS/Throttle** - Зелёная полоса пропорциональная открытию дросселя
- **Coolant Temp** - Последний LED (синий → зелёный → красный при 60-110°C)
- **Rev Limiter** - Жёлтое пульсирование при активации
- **Launch Control** - Специальная индикация (если активен)
- **Shift Light** - Синие мигающие LED при достижении точки переключения

### Дополнительные индикаторы:

- **Low Oil Pressure** - Красно-белая строба (при TPS >40% и давлении <2 bar)
- **Engine Warming** - Синее "дыхание" (при температуре <60°C)
- **Flat Shift Active** - Специальная анимация

## Веб-интерфейс

Подключитесь к WiFi сети **CANLED_AP** (пароль: `canled123`) и откройте браузер:

```
http://192.168.4.1/
```

### API Endpoints:

- `GET /api/state` - Текущие параметры в реальном времени
- `GET /api/stats` - Статистика поездки
- `POST /api/config` - Изменение настроек (redline, shift point, яркость)
- `GET /api/export/csv` - Экспорт данных в CSV

### WebSocket (Real-time):

Подключитесь к `ws://192.168.4.1:81` для получения данных в реальном времени (10 Hz):

```json
{
  "rpm": 4500,
  "throttle": 65,
  "brake": 0,
  "coolant": 85.5,
  "oil_pressure": 4.5,
  "rev_limiter": false,
  "ignition": true,
  "max_rpm": 7200,
  "avg_rpm": 3450
}
```

## Диагностика

### Проверка подключения:

1. Подключите ESP32 к компьютеру через USB
2. Откройте монитор порта:
   ```bash
   pio device monitor -b 115200
   ```
3. Вы должны увидеть:
   ```
   CAN bus started at 1 Mbps
   CAN: ID 0x5F0 DLC8 DATA ... (для Protocol 1)
   CAN: ID 0x2000 DLC8 DATA ... (для Protocol 2)
   ```

### Проблемы и решения:

**Нет данных с CAN шины:**
- Проверьте правильность подключения CAN_H и CAN_L
- Убедитесь, что CAN терминаторы установлены (120 Ом на каждом конце)
- Проверьте, что в PCLink включен Generic Dash Stream
- Проверьте скорость CAN шины (должна быть 1 Mbps)

**Неправильные значения:**
- Убедитесь, что `CAN_PROTOCOL` соответствует настройкам в ECU
- Protocol 1 для старых ECU (Generic Dashboard)
- Protocol 2 для новых ECU (Generic Dashboard 2)

**LED лента не показывает данные:**
- Проверьте подключение LED ленты к GPIO 4
- Убедитесь, что питание LED ленты подключено
- Проверьте, что данные приходят (мониторинг Serial)

## Расширенная настройка

### Изменение redline:

```cpp
// В src/config.h или через веб-интерфейс
state.rpmRedline = 7000; // Новая отсечка в RPM
```

### Настройка shift light:

```cpp
// В userConfig
userConfig.shiftLightRpm = 6800; // Точка переключения
```

### Кастомные CAN ID (для Protocol 1):

Если ваш ECU использует нестандартные ID, измените в `src/config.h`:

```cpp
namespace LinkGenericDashboard {
    constexpr uint32_t ID_RPM_TPS = 0x5F0; // Измените на ваш ID
    // ...
}
```

## Совместимость

### Протестированные ECU:

- ✅ Link G4+ Plugin
- ✅ Link G4X Thunder
- ✅ Link Fury X
- ✅ Link Storm
- ⚠️ Другие ECU с Generic Dashboard (требуется тестирование)

### CAN Bus требования:

- **Скорость**: 1 Mbps (фиксированная)
- **Тип**: CAN 2.0A (Standard 11-bit ID)
- **Терминаторы**: 120 Ом (обязательно!)
- **Напряжение**: 5V logic level (совместимо с ESP32)

## Примеры использования

### Дрэг-рейсинг:
- Shift light для оптимальных переключений
- Launch control индикация
- Статистика максимальных оборотов
- Мониторинг температур в реальном времени

### Трек-дни:
- Индикация warming up (синее дыхание пока двигатель не прогрелся)
- Rev limiter визуализация
- Flat shift индикация
- CSV экспорт данных сессии для анализа

### Ежедневное использование:
- Адаптивная яркость (ночной режим)
- Индикация низкого давления масла
- Температура охлаждающей жидкости
- Статистика поездки

## Дополнительная информация

### Link ECU документация:
- [PCLink Help File](https://www.linkecu.com/pclink-help/)
- [CAN Setup Guide](https://www.linkecu.com/can-bus-setup/)
- [Generic Dash Protocol Specification](https://www.linkecu.com/can-protocols/)

### Обновление прошивки OTA:

1. Подключитесь к WiFi сети CANLED_AP
2. Используйте ArduinoOTA или PlatformIO:
   ```bash
   pio run --target upload --upload-port 192.168.4.1
   ```
3. Прогресс отображается на LED ленте (синий → голубой)

## Техническая поддержка

При возникновении проблем:

1. Проверьте Serial Monitor для отладочных сообщений
2. Убедитесь, что CAN_PROTOCOL установлен правильно
3. Проверьте настройки ECU (Generic Dash должен быть включен)
4. Создайте Issue на GitHub с логами и описанием проблемы

---

**Версия документа**: 1.0
**Дата**: 2024
**Совместимость**: Firmware v2.0+ с модульной архитектурой
