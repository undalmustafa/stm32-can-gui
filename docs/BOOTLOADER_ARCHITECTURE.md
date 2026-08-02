# CAN bootloader mimarisi

Bu belge imzalı A/B firmware update yolunun flash ve image sözleşmesini
dondurur. HAL'den bağımsız image doğrulama, rollback policy, boot target
orkestrasyonu ve Cortex-M7 application handoff yolu hazırdır. Flash programlama
ve gerçek kripto backend'i henüz bağlı değildir.

STM32H7A3ZITxQ, 2 MiB dual-bank user flash sağlar. RM0455'te tanımlanan 8 KiB
user-flash sektör sınırlarına göre yerleşim şöyledir:

| Bölge | Başlangıç | Bitiş (exclusive) | Boyut | Bank |
|---|---:|---:|---:|---:|
| Bootloader | `0x08000000` | `0x08020000` | 128 KiB | 1 |
| Application A | `0x08020000` | `0x08100000` | 896 KiB | 1 |
| Application B | `0x08100000` | `0x081E0000` | 896 KiB | 2 |
| Boot control | `0x081E0000` | `0x08200000` | 128 KiB | 2 |

Kaynak: [ST RM0455 — STM32H7A3/7B3 reference manual](https://www.st.com/resource/en/reference_manual/dm00463927-stm32h7a3-b3-and-stm32h7b0-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf).

Sabitler `Core/Inc/boot_memory_map.h` içindedir. Derleme zamanı assertion'ları
bölgeler arasında boşluk, taşma veya çakışma oluşmasını engeller.

## Image düzeni

Her slot kendi adresine link edilmiş bağımsız bir application image taşır:

```text
slot base + 0x000  128-byte signed manifest
slot base + 0x080  reserved header padding
slot base + 0x400  Cortex-M7 vector table ve image payload
```

1 KiB header, bu target'taki 171-word vector tablosunun doğal Cortex-M7
hizasını korur. Linker vector tablosunun 1 KiB rezervi aşmasını reddeder.
Manifest little-endian alanlardan oluşur:

| Offset | Boyut | Alan |
|---:|---:|---|
| `0x00` | 4 | Magic `IMG1` (`0x31474D49`) |
| `0x04` | 2 | Manifest format version |
| `0x06` | 2 | Header size, ilk sürümde 1024 |
| `0x08` | 4 | Image payload size |
| `0x0C` | 4 | Vector table address |
| `0x10` | 4 | Thumb reset-handler address |
| `0x14` | 4 | Monotonic security counter |
| `0x18` | 4 | Product build version |
| `0x1C` | 4 | Flags; ilk sürümde sıfır olmak zorunda |
| `0x20` | 32 | Image payload SHA-256 digest |
| `0x40` | 64 | İlk 64 manifest byte'ının imzası |

İmza, digest dahil `0x00..0x3F` canonical bölgesini kapsar. Böylece image
boyutu, yerleşim adresleri, security counter, build version ve digest birlikte
yetkilendirilir. İmza algoritması ve public-key saklama backend'i ayrı dilimde
bağlanacaktır; doğrulayıcı callback yoksa image fail-closed reddedilir.

## Boot öncesi doğrulama

`Boot_Image_Validate()` sırasıyla şunları zorunlu tutar:

1. Slot, reader, digest, signature ve RAM-region bağımlılıkları geçerli.
2. Magic, format, sabit header boyu ve reserved flags doğru.
3. Image payload slot sınırları içinde ve integer overflow yok.
4. Vector table tam olarak `slot base + 1024` adresinde.
5. Security counter, kalıcı minimum değerden düşük değil.
6. Initial MSP sekiz-byte hizalı ve izin verilen RAM bölgelerinden birinde.
7. Vector reset handler ile manifest entry aynı, Thumb biti set ve entry image
   sınırları içinde.
8. Manifest imzası geçerli.
9. Image payload SHA-256 digest'i manifest ile aynı.

Başarısız doğrulama application koduna hiçbir şekilde dallanmaz. Her hata
ayrı sonuç koduyla boot policy/recovery katmanına bildirilir.

## Redundant boot-control ve rollback

Boot-control bölgesinin ilk iki 8 KiB sektörü birbirinden bağımsız record
taşır:

- Record 0: `0x081E0000`
- Record 1: `0x081E2000`

Her record 64 byte'tır ve magic/format/size, wrap-safe generation,
confirmed/pending slot, kalan deneme sayısı, security counter, build version,
CRC-32 ve commit marker içerir. Reserved alanların tamamı sıfır olmak
zorundadır.

Yeni state yazılırken eski record korunur. Diğer sektör silinir, record'un
`0..59` byte'ı programlanır ve `0xC04E7A5A` commit word'ü en son yazılır.
Power loss commit'ten önce oluşursa yeni record geçersiz kalır ve önceki
generation seçilir. CRC bozuk, yarım yazılmış veya aynı generation ile farklı
state taşıyan record'lar fail-closed reddedilir.

Update ve rollback akışı:

1. Yalnızca confirmed slottan farklı ve security counter'ı kalıcı minimumdan
   büyük image pending yapılabilir.
2. Pending image için üç boot hakkı verilir.
3. Her application jump öncesinde hak azaltılır ve yeni state kalıcılaştırılır.
4. Application kendi slot/security/build kimliğini doğrulayarak confirmation
   verir; pending slot confirmed olur ve anti-rollback minimumu yükselir.
5. Üç denemede confirmation gelmezse veya pending image doğrulaması başarısızsa
   pending temizlenir ve son confirmed image seçilir.
6. Confirmed image de geçersizse başka bir unconfirmed image otomatik
   çalıştırılmaz; bootloader recovery modunda kalır.

Bu katman flash HAL çağrısı yapmaz. Record erase/program ve application
confirmation taşıması sonraki backend dilimlerinde bağlanacaktır.

## Linker profilleri ve application handoff

Tek section yerleşimi `STM32H7A3ZITXQ_FLASH.ld` içinde tutulur; hedef profiller
yalnızca flash sınırlarını tanımlar:

- `linker/STM32H7A3ZITXQ_BOOT.ld`: `0x08000000..0x08020000`
- `linker/STM32H7A3ZITXQ_SLOT_A.ld`: payload
  `0x08020400..0x08100000`
- `linker/STM32H7A3ZITXQ_SLOT_B.ld`: payload
  `0x08100400..0x081E0000`

Application linker profilleri slot başındaki 1 KiB manifest/header alanını
bilerek link dışında bırakır. `make slot-a` ve `make slot-b` ayrı adreslere
linklenen application artifact'lerini üretir. Bu target derlemeleri imzalı
manifest paketleyici tamamlanana kadar doğrudan flashlanmamalıdır.

Startup vector sembolü dışa açılır ve `SystemInit()` VTOR'u linker'ın seçtiği
`g_pfnVectors` adresine kurar. Böylece standalone, Slot A ve Slot B aynı startup
kodunu sabit offset makroları olmadan kullanır.

`Boot_Target_Run()` şu fail-closed sıralamayı uygular:

1. İki boot-control record'u okur ve newest valid generation'ı seçer.
2. Yalnızca state içinde confirmed veya pending olarak kayıtlı slot kimliğini,
   security counter'ını ve build version'ını kabul eder.
3. Rollback policy state'i değiştirdiyse yeni state kalıcılaştırılmadan hiçbir
   application jump çağrısı yapmaz.
4. Seçilen image için ikinci handoff preflight kontrolünü çalıştırır.
5. Platform jump callback'ine doğrulanmış vector/MSP/entry planını verir.

Cortex-M7 backend'i önce bootloader'ın periferik/DMA quiesce callback'ini
çalıştırır ve başarısızlığı halinde jump'ı iptal eder; ardından global interrupt
maskesi, SysTick durdurma, tüm NVIC
enable/pending bitlerini temizleme, cache kapatma, VTOR kurulumu ve bariyerleri
uygular. Son adımda CONTROL/PSP/BASEPRI/FAULTMASK durumunu reset değerine getirir,
application MSP'sini yükler ve Thumb reset handler'a dallanır. Dal geri dönerse
backend interrupt'ları kapatıp güvenli halt durumunda kalır.

## Sıradaki dilimler

1. Flash erase/program backend'i ile power-loss kabul noktalarını ekle.
2. UDS download/routine servisleri ve GUI Flash sekmesini bağla.
3. Release image packager, offline signing ve gömülü public-key doğrulamasını
   CI/release zincirine ekle.

Mevcut `STM32H7A3ZITXQ_FLASH.ld` standalone application geliştirme düzenini
korur. Boot flash backend'i ve signed artifact zinciri tamamlanmadan bu
belgedeki adreslerle hedef flashlama yapılmamalıdır.
