# Firmware Güvenilirlik Roadmap'i

Tarih: 2026-07-31

Bu roadmap, `/home/musti/Documents/plan.txt` içindeki bulguların mevcut kodla
yeniden doğrulanmış halidir. Öncelik yeni özellik değil; ölçülebilir,
tekrarlanabilir ve arıza halinde güvenli davranan bir firmware tabanı
oluşturmaktır.

## Öncelik ilkesi

Sıralama şu risk çarpanına göre yapılır:

`öncelik = güvenlik etkisi × oluşma olasılığı × teşhis zorluğu`

Bir fazın "tamamlandı" sayılması için kodun yazılması yetmez. Host testi,
hedef derlemesi ve ilgili donanım kabul testi de geçmelidir.

## Faz 0 — Saat doğruluğu ve crash kanıtı

Durum: Kod tamamlandı, hedef donanım kabul testi bekliyor.

### 0.1 Harici referanslı CAN saati

- PLL kaynağı HSI yerine Nucleo ST-LINK'in `PH0/OSC_IN` üzerindeki 8 MHz
  MCO'suna taşındı.
- PLL giriş frekansı yaklaşık 2.667 MHz, VCO 256 MHz, SYSCLK 64 MHz ve PLL1Q
  32 MHz'tir.
- Classic CAN nominal zamanlaması 500 kbit/s ve örnekleme noktası %81.25
  olarak korunmuştur.
- Saat ve bitrate ilişkileri `_Static_assert` ile derleme zamanında
  korunmaktadır.
- `can_gui.ioc` aynı PLL topolojisini ifade edecek şekilde güncellendi.

Kabul testi:

1. PH0 üzerinde 8 MHz MCO'yu osiloskop/frekans sayacı ile doğrula.
2. FDCAN TX bit süresini en az 1.000 frame boyunca ölç; hedef 2 us.
3. Soğuk/sıcak başlangıçta en az 30'ar power-cycle uygula.
4. 500 kbit/s, iki uçta 120 ohm ve uzun kablo ile error counter artışı
   olmadığını doğrula.

### 0.2 Retained CPU fault kaydı

- NMI, HardFault, MemManage, BusFault ve UsageFault MSP/PSP exception
  frame'ini ayırarak kaydeder.
- Temel frame'deki R0-R3, R12, LR, PC ve xPSR ile CFSR, HFSR, DFSR, AFSR,
  MMFAR, BFAR, SHCSR ve ICSR BKPSRAM'de CRC ve commit marker ile tutulur.
- Floating-point extended frame ve stacking fault durumları ayrı ele alınır.
- `Error_Handler()` çağrı adresini kaydedip `NVIC_SystemReset()` üretir.
- Sonraki boot kaydı bir kez tüketir; PC/fault türü ve CFSR/HFSR retained
  event log'a aktarılır.
- Host testleri geçerli kayıt, tek-seferlik tüketim, CRC bozulması ve geçersiz
  fault türünü kapsar.

Kabul testi:

1. Debug build'de kontrollü UsageFault ve kesin olmayan adres BusFault'u
   tetikle.
2. Reset sonrası event log'daki PC'yi ELF/map ile kaynak satırına çöz.
3. Bozuk stack senaryosunda ikinci bir fault döngüsü oluşmadığını doğrula.
4. Art arda üç fault sonrası en yeni kaydın deterministik olduğunu doğrula.

## Faz 1 — Fail-safe başlangıç ve çıkış güvenliği

Durum: Yazılım dilimi tamamlandı, hedef donanım fault-injection testi
bekliyor. TIM1/TIM8 donanımsal break geçişi ayrıca açık.

### 1.1 Başlangıç state machine'i

- [x] GPIO safe seviyeleri ve saat ağacından hemen sonra IWDG'yi başlat.
- [x] FDCAN, I2C, SPI, TIM2 ve TIM3 init fonksiyonlarını ayrıntılı sonuç
  döndüren arayüzlere çevir.
- [x] Kaynak bazında `attempted / ready / failed / degraded` maskeleri ve ilk
  hata aşamasını tut.
- [x] PWM ve capture bağımlılıklarını hazır değilken başlatma.
- [x] SPI/I2C hazır değilse TIC12400/RTC driver transaction'larını engelle.
- [x] COM init hatasını ölümcül olmaktan çıkar.
- [x] Degraded başlangıcı retained event log ve sarı LED ile görünür yap.
- [x] IWDG'yi zorunlu kaynak olarak tut; IWDG init hatası hâlâ fataldir.

Kabul kriteri: Her çevrebirim ayrı ayrı söküldüğünde sistem reset döngüsüne
girmeden CAN health/status üzerinden arızayı bildirmeli; güvenli olmayan çıkış
aktif kalmamalıdır.

### 1.2 PWM safe-state

- [x] Yazılım hatası/reset yolunun ilk işi PWM kanalını kapatmak olmalı.
- [x] TIM2 CH1'i durdurup PA0'ı GPIO low yapan, HAL durumundan bağımsız acil
  safe-state yolu ekle.
- [x] Bu yolu CPU fault, `Error_Handler`, TIM2 init hatası ve PWM stop HAL
  hatasında çağır.
- [x] PWM self-test'i PWM/capture hazır değilken reddet.
- IWDG süresi, ölçülen WCET ve recovery sürelerine göre yeniden
  boyutlandırılmalı.
- Üretim donanımı için TIM1/TIM8 `BRK` girişine geçiş ve harici fault hattı
  ayrı bir donanım değişikliği olarak ele alınmalı.
- Window watchdog kararı, önce loop timing histogramı toplandıktan sonra
  verilmelidir.

Kabul kriteri: Main-loop stall, fault ve init hatasında PWM'in güvenli seviyeye
geçiş süresi ölçülmüş ve dokümante edilmiş olmalıdır.

Hedef kabul testi:

1. TIM2 init aşamalarını tek tek hata döndürecek şekilde debugger ile enjekte
   et; reset döngüsü olmadığını ve PA0'ın low kaldığını ölç.
2. I2C/SPI init hatalarında ana döngü, CAN ve watchdog heartbeat'lerinin
   çalıştığını doğrula.
3. Event log'da `APP_LOG_EVENT_STARTUP_DEGRADED` kaydındaki failed mask ve
   stage kodunu doğrula.
4. HardFault sırasında PA0 high ise safe-state gecikmesini osiloskopla ölç.

## Faz 2 — CAN alım dayanıklılığı ve zaman kanıtı

Durum: Bekliyor; Faz 1'e bağımlı.

### 2.1 CAN RX

- RX FIFO0 derinliğini message RAM bütçesine göre artır.
- `RX_FIFO0_NEW_MESSAGE`, watermark/full ve `MESSAGE_LOST` bildirimlerini
  etkinleştir.
- ISR yalnızca bounded iş yapmalı; parse/command execution main-loop
  context'inde kalmalı.
- Lost/full/watermark sayaçlarını health telemetry ve event log'a ekle.
- Burst, bus-off sırasında RX ve FIFO overflow host/HIL testleri ekle.

Kabul kriteri: 500 kbit/s'te tanımlanan maksimum burst boyunca frame kaybı
olmamalı; kapasite üstü trafik bilinçli olarak düşürülmeli ve sayaçta görünür
olmalıdır.

### 2.2 WCET ve latency ölçümü

- DWT CYCCNT ile main loop ve her servis için current/min/max/overrun tut.
- SPI/I2C timeout'larını servis bütçesiyle ilişkilendir.
- CAN command-to-ACK latency histogramı üret.
- Ölçümleri heartbeat/diagnostic frame'lerine ve GUI grafiğine taşı.

Kabul kriteri: p50/p95/p99/max loop ve ACK gecikmesi, normal yük ve enjekte
edilmiş çevrebirim arızası için raporlanmalıdır.

### 2.3 NVIC öncelik sözleşmesi

- CAN, TIC12400 EXTI, SysTick ve gelecekteki timer interrupt'ları için yazılı
  öncelik tablosu oluştur.
- ISR içinde bloklayıcı HAL çağrısı yapılmamasını kod sözleşmesine ekle.
- Timeout kaynağının kendisini engelleyen öncelik kombinasyonlarını test et.

## Faz 3 — Test ve statik kalite kapıları

Durum: Faz 1 ile paralel yürütülebilir.

- `can_recovery.c` için HAL-stub'lı bus-off recovery testleri ekle.
- `can_app.c`, `rtc_app.c` ve `tic12400_probe.c` için state-machine odaklı
  test seam'leri çıkar.
- `-Wextra -Wconversion -Wshadow -Wundef` uyarılarını temizle; ardından
  `-Werror` kapısını aç.
- Cppcheck veya clang-tidy raporunu CI artifact'i, kritik bulguları gate yap.
- `.su` dosyalarından worst-case stack raporu üret; 1 KiB stack varsayımını
  ölçümle doğrula.
- Modül header'larına çağrı bağlamını yaz: main-loop only, ISR-safe veya
  internal.
- İki Python workflow'unu tek sürüm ve tek bağımlılık kurulumuyla birleştir.
- Python bağımlılıklarını kilitle ve kök lisans/third-party notices dosyalarını
  ekle.

## Faz 4 — Bakım borcu

- `CAN_Is_Control_Access_Open()` fonksiyonunu update ve salt-okuma
  fonksiyonlarına ayır.
- B1 için debounce uygula.
- Cache/MPU ve 64 MHz çalışma kararını ölçüm sonucu ile dokümante et.
- Donanım CRC kullanımını, mevcut yazılım CRC'sine göre gerçek performans ve
  bağımlılık maliyetiyle değerlendir.

## Eksikler bittikten sonraki ürün yönü

Bu bölüm şimdilik uygulama kapsamı dışındadır. Faz 0-3 kabul kriterleri
tamamlandıktan sonra önerilen sıra:

1. `can_protocol.yaml` kaynaklı DBC üretimi.
2. ISO-TP ve UDS diagnostic servisleri.
3. A/B, imzalı ve rollback destekli CAN bootloader.
4. Classic CAN'den CAN FD'ye geçiş.
5. Freshness counter ve CMAC tabanlı komut doğrulama.
6. Self-hosted runner ile nightly HIL regresyonu.

Yeni özelliklere geçiş kapısı: kritik fault kaydı, degraded startup, ölçülmüş
PWM safe-state, kayıp görünürlüğü olan CAN RX ve WCET raporunun tamamlanmasıdır.
