<p align="center">
  <img src="docs/assets/icon.png" width="128" height="128" alt="C Editor simgesi">
</p>

<h1 align="center">C Editor</h1>

<p align="center">Mac için sade ve hızlı C editörü. Kodunu yaz, <b>⌘R</b>’ye bas, çalışsın.</p>

<p align="center">
  <a href="https://github.com/erhansavas/c-editor/releases"><img src="https://img.shields.io/github/v/release/erhansavas/c-editor?include_prereleases&label=s%C3%BCr%C3%BCm&color=blue" alt="Sürüm"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/erhansavas/c-editor?label=lisans&color=blue" alt="Lisans"></a>
</p>

<p align="center">
  <img src="docs/screenshots/editor.png" width="800" alt="C Editor’de döngü ve if/else içeren bir not programı; altındaki konsolda girilen notlar, Pekiyi/Geçti/Kaldı sonuçları ve 66,7 ortalaması">
</p>

C Editor, C öğrenenler için yapıldı. Bir dosya açın ya da yazmaya başlayın, **⌘R**’ye basın: program derlenir
ve kodun altındaki konsolda çalışır. Okuldaki Windows bilgisayarlarda yazılan kod da Türkçe karakterleriyle
birlikte olduğu gibi çalışır.

C Editor öğretir, sizin yerinize yazmaz: kod tamamlama, yazarken öneri ya da tek tıkla düzeltme yoktur. Hata
yaptığınızda nerede olduğunu ve satırın ne olması gerektiğini söyler; düzeltmeyi siz yaparsınız.

## Özellikler

- **Tek tuşla derle ve çalıştır:** **⌘R**, **F11** ya da sağ üstteki yeşil ▶. Girdiyi konsola yazarsınız.
- **Hatalarda Türkçe yardım:** derleyicinin mesajının altında ne olduğu ve ne yazılması gerektiği.
- **Çökmelerde neden:** program çökerse ne olduğunu ve nedenini Türkçe söyler.
- **Windows’ta yazılan kod olduğu gibi çalışır:** `system("pause")`, `getch()`, `setlocale(LC_ALL, "Turkish")`.
- **Türkçe dosyalar:** UTF-8 ve Windows-1254 dosyalarını kendi biçiminde açar ve kaydeder.
- **Sade ve hızlı:** Mac için yerel bir uygulama; renklendirme, sekmeler, kıstırarak yakınlaştırma.

## Kurulum

C Editor macOS 27 yüklü Apple silicon (M1 ve sonrası) Mac’lerde çalışır. Şu an beta aşamasındadır: günlük
kullanıma hazırdır, ama 1.0’a kadar davranışı değişebilir.

1. [Sürümler](https://github.com/erhansavas/c-editor/releases) sayfasından `C-Editor-0.5.1-macOS-arm64.dmg`
   dosyasını indirin, açın ve **C Editor**’ü **Applications** (Uygulamalar) klasörüne sürükleyin.
2. Uygulama Apple onaylı (notarize) olmadığı için macOS ilk açılışı engeller: **Sistem Ayarları › Gizlilik ve
   Güvenlik**’e gidip **Yine de Aç**’ı seçin.
3. Programları derlemek için Apple’ın ücretsiz Komut Satırı Araçları gerekir. Kurulu değilse ⌘R’ye ilk
   bastığınızda C Editor bunu söyler; **Kur**’a tıklamanız yeterli.

İndirdiğiniz dosyayı doğrulamak için: `shasum -a 256 -c C-Editor-0.5.1-macOS-arm64.dmg.sha256`

## Kullanım

### Yazarken

<p align="center">
  <img src="docs/screenshots/yazarken.png" width="800" alt="Girintisi elle yazılmamış, yalnızca Enter’larla yazılmış bir program; if/else ve for blokları soluk dikey çizgilerle gösteriliyor">
</p>

Bu kodda tek bir girinti boşluğu elle yazılmadı; her satırdan sonra yalnızca Enter’a basıldı. C Editor yalnızca
boşlukla yardım eder, kodu sizin yerinize yazmaz:

- `{` ile ya da süslü parantezsiz `if (...)`, `else`, `for (...)`, `while (...)`, `do` ile biten satırdan sonra
  yeni satır bir kademe içeriden başlar; o tek komutun ardından geri döner.
- `}` kendi `{` satırıyla hizalanır: boş satıra yazınca da, önünde Enter’a basınca da.
- Girintide Backspace bir kademe (4 boşluk) birden siler.
- Soluk dikey çizgiler hangi satırın hangi bloğa ait olduğunu gösterir.

### Hata yapınca

<p align="center">
  <img src="docs/screenshots/hatalar.png" width="800" alt="C Editor’de iki hata ve altlarında Türkçe açıklamaları">
</p>

Hatalı satırlar işaretlenir ve imleç ilk hataya gider. Derleyicinin mesajları terimleri tanıyın diye İngilizce
kalır; altlarında Türkçe açıklaması çıkar. Mesaja tıklayınca o satıra gidilir. Program çalışırken çökerse konsol
ne olduğunu (ör. segmentation fault) ve nedenini Türkçe söyler.

### Windows’tan gelen kod

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

Yeni dosyalar Windows’un kaydettiği biçimdedir (Windows-1254, CRLF), böylece okuldaki bilgisayarlarda da düzgün
açılır. Biçim durum çubuğundan değiştirilebilir.

### Kısayollar

| Kısayol | İş |
| --- | --- |
| **⌘R** ya da **F11** | Derle ve çalıştır |
| **Ctrl+C** | Çalışan programı durdur |
| **⌘N** / **⌘T** / **⌘O** | Yeni dosya / yeni sekme / aç |
| **⌘S** / **⌥⌘S** | Kaydet / farklı kaydet |
| **⌘W** / **⌘⇧T** | Sekmeyi kapat / kapatılan sekmeyi yeniden aç |
| **⌃⇥** / **⌃⇧⇥** | Sonraki / önceki sekme |
| **⌘1**…**⌘8** / **⌘9** | O sekmeye git / son sekmeye git |

## Kaynaktan derleme

Apple silicon, macOS 27 ve Xcode 27 gerekir.

```sh
./build.sh              # bu Mac için uygulama: build/release/
./build.sh debug        # ASan/UBSan ile uygulama: build/debug/
./build.sh test         # ASan/UBSan altında testler
./build.sh dist         # tüm Apple silicon Mac’ler için DMG ve SHA-256: build/dist/
./build.sh --install    # uygulamayı /Applications klasörüne kurar
```

## Sorun bildirme

Bir hata bulduysanız ya da bir öneriniz varsa [Issues](https://github.com/erhansavas/c-editor/issues)
sayfasından yazın. Hangi kodu yazdığınızı ve ne beklediğinizi eklemeniz yeterli.

## Lisans

Apache License 2.0. Bkz. [LICENSE](LICENSE). Değişiklikler: [CHANGELOG.md](CHANGELOG.md).
