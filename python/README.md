# STM32 CAN GUI

STM32H7 tabanlı CAN/RTC uygulamasını PCAN üzerinden yapılandıran,
izleyen ve olay kayıtlarını bilgisayara aktaran PySide6 masaüstü
uygulamasıdır.

Bu final sürümde başlangıçta yaklaşık 2000 satır olan tek Python
dosyası sorumluluklarına ayrılmıştır. Giriş dosyası `can_gui.py` 260
satırdır ve yalnızca servis, controller ve panelleri birbirine bağlar.

## Kurulum

Windows Komut İstemi veya PowerShell'i bu klasörde açın:

```powershell
py -m venv .venv
.venv\Scripts\activate
py -m pip install -r requirements.txt
py can_gui.py
```

Sonraki çalıştırmalarda `run_gui.bat` dosyası da kullanılabilir.

PCAN adaptörünün Windows sürücüsü/PCAN-Basic bileşeni sistemde kurulu
olmalıdır. Varsayılan kanal `PCAN_USBBUS1`, varsayılan bitrate
`500000 bit/s`'dir.

> `can_gui.py` tek başına kopyalanmamalıdır. `can_gui_app` klasörü aynı
> dizinde tutulmalıdır.

## Dizin yapısı

```text
stm32_can_gui_final/
├── can_gui.py
├── run_gui.bat
├── requirements.txt
├── can_gui_app/
└── tests/
```

## Modül sorumlulukları

| Modül | Sorumluluk |
|---|---|
| `can_gui.py` | Uygulama composition root'u, hata mesajları ve controller bağlantıları |
| `protocol.py` | CAN ID, komut, durum ve payload sabitleri |
| `can_session.py` | PCAN bağlantısı, TX/RX ve taşıma sayaçları |
| `can_health.py` | CAN health durum makinesi, BUS_HEAVY/BUS_OFF izleme ve log tekrar bastırma |
| `can_connection_panel.py` | Kanal/bitrate formu ve CAN Health görünümü |
| `can_app_controller.py` | Slot/LED komutları ve STM32 uygulama durum mesajları |
| `can_app_panel.py` | Slot/LED formu ve Values durum görünümü |
| `slot_widget.py` | Tekrar kullanılabilir slot yapılandırma widget'ı |
| `rtc_controller.py` | RTC/alarm CAN protokolü, diagnostics ve durum yönetimi |
| `rtc_panel.py` | RTC takvim/alarm formu ve Values görünümü |
| `csv_event_logger.py` | Bilgisayar tarafındaki günlük GUI olay CSV dosyası |
| `stm32_log_sync.py` | STM32 RAM olay kayıtlarının CAN üzerinden alınması ve doğrulanması |
| `event_log_panel.py` | Log ayarları ile GUI/STM32 log durum göstergeleri |
| `main_window_view.py` | Config/Values sekmeleri ve ana pencere yerleşimi |
| `application_timers.py` | RX, CAN Health ve STM32 log senkronizasyon timer'ları |

## Periyodik işlemler

| İşlem | Periyot |
|---|---:|
| CAN RX polling | 50 ms |
| STM32 log senkronizasyonu | 100 ms |
| CAN Health kontrolü | 250 ms |

## Log dosyaları

- `events_YYYYMMDD.csv`: GUI, CAN Health, RTC ve uygulama olayları.
- `stm32_events_YYYYMMDD_HHMMSS_ffffff.csv`: STM32 RAM logundan CRC ve
  commit marker doğrulamasıyla aktarılan kayıtlar.

Varsayılan konum `logs` klasörüdür. GUI'deki **Log Klasörü Seç**
düğmesiyle değiştirilebilir.

## Testler

Sözdizimi kontrolü:

```powershell
py -m compileall -q can_gui.py can_gui_app tests
```

Tüm host testlerini Windows Komut İstemi'nde çalıştırmak için:

```cmd
for %f in (tests\test_gui_*.py) do py "%f"
```

Test paketi protokol sabitlerini, CAN taşımasını, CAN Health durum
makinesini, RTC/alarm controller'ını, slot/LED controller'ını, iki CSV
log katmanını, panelleri, pencere yerleşimini ve timer periyotlarını kapsar.

## Donanım kabul kontrolü

1. PCAN bağlantısında `WAIT_RX -> ACTIVE` geçişini doğrulayın.
2. Slot 1/2 sayaçlı mesajlarını ve cycle time değerlerini kontrol edin.
3. LED1/LED2 ON/OFF komutlarını kontrol edin.
4. RTC okuma/yazma, otomatik hafta günü ve alarm akışını deneyin.
5. GUI ve STM32 CSV loglarının oluştuğunu doğrulayın.
6. CAN hattını kısa devre ederek `BUS_HEAVY` durumunu gözlemleyin.
7. Hattı düzelttikten sonra PCAN USB'yi sökmeden `OK/ACTIVE` durumuna
   dönüldüğünü ve RX sayacının ilerlediğini doğrulayın.
