# UDS diagnostic sözleşmesi

UDS fiziksel adresleme, Classic CAN üzerinde ISO-TP ile çalışır:

- Tester → MCU request ID: standard `0x7E0`
- MCU → tester response ID: standard `0x7E8`
- ISO-TP taşıma kapasitesi: 512 byte

## Desteklenen servisler

| SID | Servis | Destek |
|---:|---|---|
| `0x10` | Diagnostic Session Control | Default `0x01`, programming `0x02`, extended `0x03` |
| `0x22` | Read Data by Identifier | Tek veya çoklu DID |
| `0x31` | Routine Control | Inactive-slot erase `0xFF00` |
| `0x34` | Request Download | 32-bit adres + 32-bit artifact boyutu |
| `0x36` | Transfer Data | 256 byte'a kadar veri + block counter |
| `0x37` | Request Transfer Exit | İmza/doğrulama ve pending-slot commit kapısı |
| `0x3E` | Tester Present | Subfunction `0x00` |

`0x10` ve `0x3E` için suppress-positive-response biti desteklenir. Extended
Default dışındaki session'lar 5 saniyelik S3 timeout sonunda default session'a döner. P2Server 50
ms, P2*Server 5000 ms olarak raporlanır.

## Firmware indirme sözleşmesi

Programlama servisleri yalnızca programming session `0x02` içinde çalışır.
Akış `31 01 FF00` erase başlangıcı, `31 03 FF00` sonuç sorgusu, `0x34`, sıralı
`0x36` blokları ve `0x37` transfer çıkışı şeklindedir. Erase routine yanıtı
durum (`0=ready`, `1=in progress`, `2=failed`), hedef slot, slot başlangıç
adresi ve kapasitesini döndürür. Cihaz çalışan slotun tersini kendi seçer;
tester aktif slotu seçemez.

Download artifact boyutu 16 byte flashword hizalı, en az 1 KiB header içeren
ve inactive slot kapasitesini aşmayan bir değerdir. `0x34` yanıtındaki maksimum
block length `258` byte'tır: SID + block sequence counter + en çok 256 byte
veri. Tekrarlanan son counter idempotent ACK alır; atlanan veya sırası bozulan
counter `0x73` ile reddedilir.

İlk 1 KiB header transfer sırasında RAM'de tutulur. Payload önce inactive
slota yazılır; `0x37` callback'i imza/digest doğrulamasını, header'ın son
yazılmasını ve redundant boot-control pending kaydını tamamlamadıkça transfer
başarılı sayılmaz. Programming session'dan çıkış ve S3 timeout açık indirmeyi
abort eder. Bu callback'in STM32 flash ve gömülü public-key implementasyonu
bootloader signing adımında bağlanacaktır; callback yokken servis `0x22`
conditions-not-correct ile fail-closed kalır.

## Data Identifier alanları

Tüm çok-byte alanlar UDS gereği big-endian taşınır.

| DID | Ad | Veri düzeni |
|---:|---|---|
| `0xF100` | Protocol info | UDS version (u8), CAN protocol version (u8), log version (u8), ISO-TP capacity (u16), request ID (u16), response ID (u16) |
| `0xF101` | Startup health | expected/ready/failed mask (3×u32), first failed resource (u32), first failure result (u32), degraded (u8) |
| `0xF102` | Runtime health | uptime, latched issues, rejected frames, RX lost, TX overflow, ISO-TP protocol errors, ISO-TP transport failures (7×u32) |
| `0xF103` | Reset reason | decoded flags (u32), raw RCC RSR (u32), capture count (u32) |

## Negative response davranışı

Sunucu desteklenmeyen servis için `0x11`, subfunction için `0x12`, hatalı
uzunluk için `0x13`, uzun response için `0x14`, sağlanamayan koşul için `0x22`
ve bilinmeyen DID için `0x31` döndürür. Firmware indirme ayrıca sıra hatası
`0x24`, yetki reddi `0x33`, transfer suspend `0x71`, programlama hatası `0x72`,
block counter hatası `0x73` ve yanlış session `0x7F` kullanır. Negative response formatı
`7F <request SID> <NRC>` şeklindedir.

## Python istemci ve GUI davranışı

Masaüstü uygulaması diagnostic request ve flow-control frame'lerini standard
`0x7E0` ID'sinden gönderir; `0x7E8` yanıtlarını mevcut 50 ms CAN RX döngüsünde
işler. Bus için ikinci bir reader thread açılmaz. ISO-TP response timeout'u 1
saniyedir; sequence hatası, geçersiz PCI/uzunluk ve taşıma hatası mevcut işlemi
fail-closed sonlandırır.

Diagnostics sekmesi dört DID'i Classic CAN single-frame request sınırını
aşmamak için iki grupta okur: `F100+F101`, ardından `F102+F103`. Her saniye bir
grup sorgulanır. Uzun ECU yanıtları first/consecutive frame olarak birleştirilir
ve tester flow-control CTS (`BS=0`, `STmin=0`) gönderir. Aynı haberleşme hatası
event log'a yalnızca durum değişiminde yazılır; başarılı cevap geldiğinde
recovery olayı üretilir.
