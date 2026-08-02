# Statik Analiz Sözleşmesi

Firmware uygulama kaynakları Cppcheck ile iki seviyede analiz edilir:

1. Rapor taraması `warning`, `style`, `performance` ve `portability`
   bulgularını `build/static-analysis/cppcheck.txt` dosyasına yazar.
2. Kalite kapısı `error`, `warning`, `performance` ve `portability`
   bulgularını `build/static-analysis/cppcheck.xml` dosyasına yazar ve herhangi
   bir bulguda başarısız olur. Stil önerileri görünür kalır ancak CI'ı durdurmaz.

Analiz STM32H7'nin 32-bit ARM ABI'sini ve firmware derleme tanımlarını kullanır.
`Core/Src` proje kapsamındadır. `Drivers/` altındaki HAL/CMSIS üçüncü taraf kodu
rapor ve kapı dışında tutulur; derleyicinin firmware job'undaki strict warning
kapısı bu kaynakları ayrıca doğrular.

Yerel çalıştırma:

```sh
make static-analysis
```

Farklı bir Cppcheck ikilisi veya çıktı dizini gerektiğinde:

```sh
make static-analysis CPPCHECK=/path/to/cppcheck \
  STATIC_ANALYSIS_DIR=/tmp/can-gui-analysis
```

CI, metin ve XML raporlarını job başarılı veya başarısız olsa da artifact olarak
saklar. Suppression yalnızca doğrulanmış araç yanılgıları veya dış ABI
sözleşmeleri için, dar kapsamlı ve gerekçesi kod yanında açıklanmış şekilde
eklenmelidir.
