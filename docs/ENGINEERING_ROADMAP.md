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

Durum: 2.1, 2.2 ve 2.3 yazılım dilimleri tamamlandı; hedef HIL kabul testleri
bekliyor.

### 2.1 CAN RX

- [x] RX FIFO0 derinliğini 3'ten 32 elemana artır. Bir standard filter word,
  iki extended-filter word, 128 RX FIFO word ve 12 TX FIFO word ile toplam
  kullanım 2560 word SRAMCAN bütçesinin 143 word'üdür.
- [x] FIFO0'ı blocking modda tut; kapasite üstünde en eski komutu ezmek yerine
  yeni frame'i bilinçli olarak reddet.
- [x] 24-eleman watermark ile `RX_FIFO0_NEW_MESSAGE`, watermark/full ve
  `MESSAGE_LOST` bildirimlerini
  etkinleştir.
- [x] ISR yalnızca flag eşleme, fill-level örnekleme ve 32-bit sayaç artırma
  yapmalı; parse/command execution main-loop
  context'inde kalmalı.
- [x] Lost/full/watermark sayaçlarını `0x560` health telemetry, GUI CAN health
  durumu, live diagnostics ve 100 ms'de bir birleştirilen event log'a ekle.
- [x] 32-frame burst ile full/lost sayaç davranışının host testlerini ekle.
- [ ] 500 kbit/s gerçek bus burst, bus-off sırasında RX ve kapasite üstü FIFO
  overflow HIL testlerini tamamla.

Kabul kriteri: 500 kbit/s'te tanımlanan maksimum burst boyunca frame kaybı
olmamalı; kapasite üstü trafik bilinçli olarak düşürülmeli ve sayaçta görünür
olmalıdır.

Hedef kabul testi:

1. İkinci CAN node'dan sabit extended komut ID'siyle art arda 32 adet 8-byte
   Classic CAN frame gönder; `message_lost_events == 0` ve
   `max_fifo_fill <= 32` doğrula.
2. Main-loop'a kontrollü gecikme enjekte ederek 33 ve üzeri frame gönder;
   blocking FIFO'nun eski komutları koruduğunu, `full` ve `message_lost`
   sayaçlarının arttığını doğrula.
3. FIFO doluyken bus-off üret; recovery sonrası RX bildirimlerinin yeniden
   etkin olduğunu ve yeni frame'lerin işlendiğini doğrula.
4. `0x560` payload'ındaki watermark/full/lost sayaçlarını ve event log'daki
   `0x010A..0x010C` kayıtlarını aynı trafikle karşılaştır.

### 2.2 WCET ve latency ölçümü

- [x] DWT CYCCNT ile main loop ve sekiz servis için current/min/max/budget ve
  overrun count tut. Sayaç wraparound farkını unsigned çıkarımla işle.
- [x] TIC12400 SPI ve PCA2131 I2C 10 ms HAL timeout'larını 12 ms servis
  bütçesine bağla; CAN için 25 ms, toplam loop için 50 ms bütçe tanımla.
- [x] CAN frame'in FIFO'dan alınmasından ACK'in transport'a teslimine kadar
  9 kovalı latency histogramı ve yaklaşık p50/p95/p99/max üret.
- [x] Servis timing özetini `0x561`, ACK özetini `0x562` diagnostic frame'ine
  taşı.
- [x] Ölçümleri GUI timing tablosuna, servis başına 120 örnekli sparkline'a,
  ACK p95 geçmiş grafiğine ve event-log decoder'ına taşı.

Kabul kriteri: p50/p95/p99/max loop ve ACK gecikmesi, normal yük ve enjekte
edilmiş çevrebirim arızası için raporlanmalıdır.

Hedef kabul testi:

1. En az 10 dakika normal yükte `MAIN_LOOP`, `TIC12400_PROBE`, `RTC` ve
   `CAN_APP` min/max değerlerini kaydet; beklenmeyen overrun olmamalı.
2. SPI MISO ve I2C SDA arızalarını ayrı ayrı enjekte et; 10 ms HAL timeout,
   12 ms servis bütçesi ve 50 ms loop bütçesinin ölçülen max değerleriyle
   uyumunu doğrula.
3. En az 10.000 geçerli ve geçersiz komut gönder; `0x562` p50/p95/p99/max
   RX-to-ACK-enqueue değerlerini GUI CSV/event kaydıyla raporla.
4. DWT'yi debugger üzerinden kapat; `0x561` DWT OFF durumunun görünür
   olduğunu ve ölçüm eksikliğinin sıfır süre gibi yorumlanmadığını doğrula.
5. Normal ve fault-injection raporlarına firmware commit'i, clock frekansı,
   CAN yükü ve ölçüm süresini ekle.

### 2.3 NVIC öncelik sözleşmesi

- [x] `NVIC_PRIORITYGROUP_4` altında safety timer `0`, SysTick `1`, FDCAN `2`,
  TIC12400 EXTI `3`, gelecekteki application timer'lar `4..14` ve user button
  `15` olacak şekilde yazılı öncelik tablosu oluştur.
- [x] Sabitleri `app_irq_policy.h` içinde tek kaynak yap; HAL config, MSP/GPIO
  init ve `can_gui.ioc` değerlerini aynı sözleşmeyle eşleştir.
- [x] ISR içinde bloklayıcı HAL, log, transport ve state-machine çağrılarını
  yasaklayan; yalnızca bounded flag/counter/status kopyasına izin veren çağrı
  bağlamı sözleşmesini ekle.
- [x] SysTick'in FDCAN, EXTI veya gelecekteki timer ile aynı/daha düşük
  preemption priority'de olduğu kombinasyonları host testinde reddet.
- [ ] Gerçek hedefte NVIC register audit ve eşzamanlı CAN/TIC12400 interrupt
  storm kabul testini tamamla.

Detaylı tablo, yeni IRQ ekleme kontrol listesi ve HIL senaryosu
`docs/INTERRUPT_POLICY.md` içindedir.

## Faz 3 — Test ve statik kalite kapıları

Durum: CAN, RTC ve TIC12400 servis host state-machine test dilimleri, strict
compiler warning, Cppcheck, release stack-budget ve API call-context kalite
kapıları tamamlandı; diğer statik kalite adımları bekliyor.

- [x] `can_recovery.c` için açık init/reset seam'i ve HAL-stub'lı bus-off
  recovery testleri ekle. 200 ms retry rate-limit, başarılı fazların tekrar
  edilmemesi, notification restore, TX-complete doğrulaması, yeni bus-off ile
  stale doğrulamanın reddi, log coalescing ve tick wrap kapsanıyor.
- [x] `can_app.c` FDCAN init zincirini gerçek üretim yolunda ayrı seam'e
  çıkar; success ve peripheral/filter/global-filter/FIFO mode/watermark/start/
  notification hata aşamalarını, transport cleanup ve startup loguyla test et.
- [x] `can_app.c` payload doğrulamasını ve B1 access-window state'ini gerçek
  üretim seam'lerine çıkar; request/update/query ayrımı, expiry ve tick wrap
  davranışını host testleriyle koru.
- [x] `can_app.c` RX FIFO orchestration, frame gate, reject/ACK sonucu ve
  sekiz-frame işlem bütçesini gerçek üretim fonksiyonu üzerinden hostta koru.
- [x] `rtc_app.c` init/reconnect, tarih-saat ve alarm write/readback, tick wrap
  ve alarm-event retry akışlarını gerçek üretim fonksiyonları üzerinden hostta
  koru; re-init state resetini ve takvim readback eşleşmesini fail-closed yap.
- [x] `tic12400_probe.c` kimlik/configuration/CRC, interrupt/fallback service,
  offline/re-init ve profile reconfiguration akışlarını gerçek üretim
  fonksiyonları üzerinden hostta koru; başarısız SPI verisini fail-closed tut.
- [x] `-Wextra -Wconversion -Wshadow -Wundef` uyarılarını temizle; host ve
  firmware Makefile'larında `-Werror` kapısını aç. Host matrisini kapı açıkken
  çalıştır; target toolchain doğrulamasını firmware build adımında tamamla.
- [x] Cppcheck raporunu metin/XML CI artifact'i yap; proje kaynaklarındaki
  error/warning/performance/portability bulgularını gate et, stil bulgularını
  rapor seviyesinde görünür tut ve üçüncü taraf HAL/CMSIS kodunu hariç bırak.
- [x] `.su` ve disassembly call graph'tan nested-IRQ worst-case stack raporu
  üret. 1 KiB varsayımını extended FP exception frame ve 256 bayt zorunlu
  marjla reddet; linker/CubeMX rezervini 2 KiB yap ve TXT/JSON CI artifact gate'i
  ekle.
- [x] 34 proje modül header'ına makine-kontrollü `MAIN_LOOP_ONLY`, `ISR_SAFE`
  ve `INTERNAL` sözleşmesi ekle; ISR seam'lerinde `FromIsr`/`RecordIsr`
  isimlendirmesini ve header kapsamını CI'da gate et.
- [x] Yinelenen Python test workflow'unu ana CI GUI job'unda birleştir;
  Python 3.13, tek GUI bağımlılık kurulumu ve Make tabanlı syntax/regresyon
  hedefi kullan; bu sözleşmeyi host testinde koru.
- [ ] Python bağımlılıklarını kilitle ve kök lisans/third-party notices
  dosyalarını ekle.
  - [x] Python 3.13 GUI zincirini doğrudan/transitif sürümlerle kilitle;
    protocol generator pin'ini güncelle ve Make/CI sözleşmesi ekle.
  - [ ] Proje lisansını kökte açıkla ve gömülü/runtime üçüncü taraf
    bileşenlerini notice dosyasında envanterle.

## Faz 4 — Bakım borcu

- [x] `CAN_Is_Control_Access_Open()` yan etkisini request, update ve salt-okuma
  fonksiyonlarına ayır.
- B1 için debounce uygula.
- Cache/MPU ve 64 MHz çalışma kararını ölçüm sonucu ile dokümante et.
- Donanım CRC kullanımını, mevcut yazılım CRC'sine göre gerçek performans ve
  bağımlılık maliyetiyle değerlendir.

## Ürün geliştirme yönü

Ürün geliştirme, kalan lisans/HIL/bakım maddelerinden önce yürütülür. Mevcut
host kalite kapıları korunur; donanım kabul eksikleri ayrı backlog olarak
izlenir. Ürün için runtime, build, test, release veya doğrulanabilir mühendislik
kanıtı sağlamayan dosyalar referans audit'inden sonra kaldırılır.

Uygulama sırası:

1. Ürün dışı bring-up/debug yollarını kaldır.
   - [x] Başlangıçtaki 1000 tekrarlı TIC12400 SPI probe döngüsünü ve sayaçlarını
     kaldır; tek kimlik okuması, register readback ve configuration CRC ile
     fail-closed başlangıcı koru.
   - [x] Firmware watchdog GDB fault-injection hook'larını, debug linker
     section'larını ve HIL stress aracını kaldır; hata kapısı host testinde
     izole test desteğiyle doğrulanmaya devam etsin.
   - [x] Ürünün comparator tabanlı anahtar yolunda kullanılmayan ham ADC
     telemetrisi, decoder, yazılım debounce ve GUI karakterizasyon durumunu
     kaldır; `0x553` engineering mesajını protokol/DBC'den çıkar.
   - [x] Modül içi tanı state'lerini public firmware API'sinden kapat ve
     tarihsel alt-roadmap/karakterizasyon dosyalarını kaldır.
2. `can_protocol.yaml` kaynaklı DBC üretimi.
   - [x] Tüm frame ID/yön/DLC/açıklamalarını, extended-ID işaretini ve komut
     value table'ını deterministik `can_gui.dbc` artifact'ine üret; Make, CI ve
     release zincirine bağla.
   - [x] Command ACK ile TIC12400 durum/switch/profil frame'lerini gerçek bit
     alanları, enum/boolean value table'ları ve mühendislik birimleriyle
     tanımla; eksik/çakışan/frame dışı şemaları jeneratörde reddet.
   - [ ] Kalan RTC, PWM, sistem, log, RX-health ve timing frame'lerini gerçek
     sinyallere; GUI komutlarını multiplexed payload alanlarına taşı.
3. ISO-TP ve UDS diagnostic servisleri.
4. A/B, imzalı ve rollback destekli CAN bootloader.
5. Classic CAN'den CAN FD'ye geçiş.
6. Freshness counter ve CMAC tabanlı komut doğrulama.
7. Self-hosted runner ile nightly HIL regresyonu.

Yakın hedef, DBC sinyal semantiğini tamamladıktan sonra ISO-TP taşımasını küçük,
host-testli bir çekirdek olarak eklemektir. UDS servisleri bu taşıma katmanının
üzerine kurulacaktır.
