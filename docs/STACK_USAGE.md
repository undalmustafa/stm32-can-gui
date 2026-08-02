# Stack Usage Budget

Release firmware stack kullanımı GCC'nin `-fstack-usage` ile ürettiği `.su`
dosyaları ve `arm-none-eabi-objdump` disassembly çağrı grafiği birlikte
incelenerek sınırlandırılır.

```sh
make CONFIG=release
make CONFIG=release stack-report
```

İkinci hedef firmware'i yalnızca güncel değilse Make bağımlılıkları üzerinden
yeniler; normal durumda `build/release/stack-usage.txt` ve
`build/release/stack-usage.json` üretir. CI bu iki dosyayı ayrı artifact olarak
saklar ve bütçe aşımında başarısız olur.

## Bütçe modeli

- Gerçek rezerv linker script'teki `_Min_Stack_Size` sembolünden okunur.
- Main-loop'un en derin doğrudan çağrı yolu hesaplanır.
- `app_irq_policy.h` içindeki aktif IRQ öncelikleri düşük öncelikten yükseğe
  doğru tam nesting kabul edilerek bu yola eklenir.
- Her exception girişi için Cortex-M7 extended floating-point frame'i ve
  alignment dahil 108 bayt ayrılır.
- `.su` bilgisi bulunmayan runtime/library sınırında tüm dış çağrı zinciri
  için 256 bayt, çözülemeyen dolaylı çağrıda 256 bayt rezerv kullanılır.
- Hesaplanan envelope sonrasında en az 256 bayt güvenlik marjı kalmalıdır.
- Dynamic veya recursive stack kullanımı fail-closed reddedilir.

Mevcut release çıktısında konservatif envelope 1288 bayttır. Önceki 1 KiB
rezerv, extended exception nesting ve zorunlu marjla yeterli değildir. Bu
modelde minimum kabul edilebilir rezerv 1544 bayttır. Bu nedenle linker ve
CubeMX sözleşmesi 2 KiB'a yükseltilmiştir; kalan ham marj 760 bayt, politika
marjı çıkarıldıktan sonraki headroom 504 bayttır.

## Sınırlar

Rapor statik ve konservatiftir; runtime watermark ölçümünün yerine geçmez.
`ignoring_inline_asm` işaretli GCC kayıtlarını ve rezerv uygulanan dış sembolleri
raporda açıkça listeler. Yeni IRQ eklendiğinde `scripts/stack_budget.json`
içindeki root ve `app_irq_policy.h` içindeki priority macro birlikte
tanımlanmalıdır. Fault/NMI kabul testi ve gerçek hedef stack watermark ölçümü
HIL kapsamında ayrıca yürütülmelidir.
