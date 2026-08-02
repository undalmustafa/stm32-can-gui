# UDS diagnostic sözleşmesi

UDS fiziksel adresleme, Classic CAN üzerinde ISO-TP ile çalışır:

- Tester → MCU request ID: standard `0x7E0`
- MCU → tester response ID: standard `0x7E8`
- ISO-TP taşıma kapasitesi: 512 byte

## Desteklenen servisler

| SID | Servis | Destek |
|---:|---|---|
| `0x10` | Diagnostic Session Control | Default `0x01`, extended `0x03` |
| `0x22` | Read Data by Identifier | Tek veya çoklu DID |
| `0x3E` | Tester Present | Subfunction `0x00` |

`0x10` ve `0x3E` için suppress-positive-response biti desteklenir. Extended
session, 5 saniyelik S3 timeout sonunda default session'a döner. P2Server 50
ms, P2*Server 5000 ms olarak raporlanır.

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
ve bilinmeyen DID için `0x31` döndürür. Negative response formatı
`7F <request SID> <NRC>` şeklindedir.
