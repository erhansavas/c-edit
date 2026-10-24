<div align="center">
  <img src="docs/assets/icon.png" width="128" height="128" alt="C Editor simgesi">
  <h1>C Editor</h1>
  <p><strong>Mac için sade ve hızlı C editörü. Kodunu yaz, ⌘R’ye bas, çalışsın.</strong></p>

  [![Sürüm](https://img.shields.io/github/v/release/erhansavas/c-editor?include_prereleases&label=s%C3%BCr%C3%BCm)](https://github.com/erhansavas/c-editor/releases)
  [![Platform](https://img.shields.io/badge/macOS_27-Apple_silicon-black?logo=apple)](#kurulum)
  [![Lisans](https://img.shields.io/badge/lisans-Apache--2.0-blue.svg)](LICENSE)
</div>

<p align="center">
  <img src="docs/screenshots/editor.png" width="760" alt="C Editor’de döngü ve if/else içeren bir not programı; altındaki konsolda girilen notlar, Pekiyi/Geçti/Kaldı sonuçları ve 66,7 ortalaması">
</p>

C Editor, C öğrenenler için yapıldı. Bir dosya açın ya da yazmaya başlayın, **⌘R**’ye basın: program derlenir
ve kodun altındaki konsolda çalışır. Okuldaki Windows bilgisayarlarda yazılan kod da Türkçe karakterleriyle
birlikte olduğu gibi çalışır.

C Editor öğretir, sizin yerinize yazmaz: kod tamamlama, yazarken öneri ya da tek tıkla düzeltme yoktur. Hata
yaptığınızda nerede olduğunu ve satırın ne olması gerektiğini söyler; düzeltmeyi siz yaparsınız.

## Neler yapar

- **Tek tuşla derle ve çalıştır:** **⌘R**, **F11** ya da sağ üstteki yeşil ▶. Girdiyi konsola yazarsınız;
  program bitince bir tuş konsolu kapatır, çalışırken **Ctrl+C** durdurur.
- **Hatalarda Türkçe yardım:** derleyicinin İngilizce mesajının altında ne olduğu ve ne yazılması gerektiği.
- **Çökmelerde neden:** program çökerse ne olduğunu (ör. segmentation fault) ve nedenini Türkçe söyler.
- **Windows’ta yazılan kod olduğu gibi çalışır:** `system("pause")`, `getch()`, `setlocale(LC_ALL, "Turkish")`.
- **Türkçe dosyalar:** UTF-8 ve Windows-1254 dosyaları kendi biçiminde açar ve kaydeder; yeni dosyalar
  okuldaki bilgisayarlarda da düzgün açılsın diye Windows-1254 ve CRLF’dir.
- **Sade ve hızlı:** Apple silicon için yerel bir uygulama; renklendirme, sekmeler, kıstırarak yakınlaştırma.

## Yazarken

<p align="center">
  <img src="docs/screenshots/yazarken.png" width="760" alt="Girintisi elle yazılmamış, yalnızca Enter’larla yazılmış bir program; if/else ve for blokları soluk dikey çizgilerle gösteriliyor">
</p>

Bu kodda tek bir girinti boşluğu elle yazılmadı; her satırdan sonra yalnızca Enter’a basıldı. C Editor
yazdıklarınıza yalnızca boşlukla yardım eder, hiçbir kodu sizin yerinize yazmaz:

- `{` ile ya da süslü parantezsiz `if (...)`, `else`, `for (...)`, `while (...)`, `do` ile biten satırdan sonra
  yeni satır bir kademe içeriden başlar; o tek komutun ardından geri döner.
- `}` kendi `{` satırıyla hizalanır: boş satıra yazınca da, önünde Enter’a basınca da. `{}` arasında Enter’a
  basınca imleç aradaki girintili satıra gelir.
- Girintide Backspace bir kademe (4 boşluk) birden siler.
- Soluk dikey çizgiler hangi satırın hangi bloğa ait olduğunu gösterir.

## Hata yapınca

<p align="center">
  <img src="docs/screenshots/hatalar.png" width="760" alt="C Editor’de iki hata ve altlarında Türkçe açıklamaları">
</p>

Hatalı satırlar işaretlenir ve imleç ilk hataya gider. Derleyicinin mesajları terimleri tanıyın diye İngilizce
kalır; altlarında Türkçe açıklaması çıkar: “Noktalı virgül (;) eksik. “;” yazılmalı.” Mesaja tıklayınca o
satıra gidilir. Tanımsız değişken, koşulda `==` yerine `=`, `if (...);`, tutmayan `%d`, kapanmamış tırnak,
eksik `}`, unutulmuş `#include` gibi sık hatalar da Türkçe açıklanır.

## Windows’tan gelen kod

| Kodunuzda | C Editor’de |
| --- | --- |
| `system("pause")` | “Devam etmek için bir tuşa basın . . .” yazar ve bir tuş bekler |
| `system("cls")` | konsolu temizler |
| `system("chcp 1254")`, `color`, `title`, `mode` | sorun çıkarmaz; Mac’te gerekmez |
| `setlocale(LC_ALL, "Turkish")` | Türkçe yerel ayar |
| `fflush(stdin)` | yazılan satırın kalanını atar, Windows’taki gibi |
| `<conio.h>`: `getch`, `getche` | tek tuş okur |
| `<windows.h>`: `Sleep`, `SetConsoleOutputCP`, `SetConsoleCP` | desteklenir |
| `#include <bits/stdc++.h>` | desteklenir |
| `int` yazılmamış `main()`, unutulmuş `#include` | Windows’taki GCC gibi hata değil, uyarı verir |

## Kurulum

C Editor beta aşamasındadır: günlük kullanıma hazırdır, ama 1.0’a kadar davranışı değişebilir.

1. [Sürümler](https://github.com/erhansavas/c-editor/releases) sayfasından
   `C-Editor-0.5.1-macOS-arm64.dmg` dosyasını indirin, açın ve **C Editor**’ü **Applications** (Uygulamalar)
   klasörüne sürükleyin.
2. Uygulama Apple onaylı (notarize) olmadığı için macOS ilk açılışı engeller: **Sistem Ayarları › Gizlilik ve
   Güvenlik**’e gidip **Yine de Aç**’ı seçin.
3. Programları derlemek için Apple’ın ücretsiz Komut Satırı Araçları gerekir. Kurulu değilse ⌘R’ye ilk
   bastığınızda C Editor bunu söyler; **Kur**’a tıklamanız yeterli.

İndirdiğiniz dosyayı doğrulamak için: `shasum -a 256 -c C-Editor-0.5.1-macOS-arm64.dmg.sha256`

## Kısayollar

| Kısayol | İş |
| --- | --- |
| **⌘R** ya da **F11** | Derle ve çalıştır |
| **⌘N** / **⌘T** / **⌘O** | Yeni dosya / yeni sekme / aç (sekmede) |
| **⌘S** / **⌥⌘S** | Kaydet / farklı kaydet |
| **⌘W** / **⌘⇧T** | Sekmeyi kapat / kapatılan sekmeyi yeniden aç |
| **⌃⇥** / **⌃⇧⇥** | Sonraki / önceki sekme |
| **⌘1**…**⌘8** / **⌘9** | O sekmeye git / son sekmeye git |
| **Ctrl+C** | Çalışan programı durdur |

## Kaynaktan derleme

Apple silicon, macOS 27 ve Xcode 27 gerekir.

```sh
./build.sh              # bu Mac için uygulama: build/release/
./build.sh debug        # ASan/UBSan ile uygulama: build/debug/
./build.sh test         # ASan/UBSan altında testler
./build.sh dist         # tüm Apple silicon Mac’ler için DMG ve SHA-256: build/dist/
./build.sh --install    # uygulamayı /Applications klasörüne kurar
```

## Lisans

Apache License 2.0. Bkz. [LICENSE](LICENSE).
