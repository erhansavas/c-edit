# Değişiklikler

## 0.5.1 — 24 Ekim 2026

- `}` işaretinin önünde Enter’a basınca `}` kendi `{` satırıyla hizalı olarak yeni satıra geçer; `{}` arasında
  Enter’a basınca imleç aradaki girintili satıra gelir. Önceden `}` yeni satırın girintisinde kalıyordu.
- Girintide Backspace bir kademe (4 boşluk) birden siler.
- Backspace ya da Delete ile silinen boş satır boşluklarını da götürür; alttaki satır, örneğin `}`, kendi
  hizasında kalır.
- README sadeleşti: üç ekran görüntüsü, hepsi koyu temada; sürüm rozeti GitHub’daki son sürümü gösterir.

## 0.5 — 24 Ekim 2026

- Koşulunda tırnak içinde parantez olan `if`/`while` satırları da (örneğin `if (c == ')')`) Enter’dan sonra
  girintilenir.
- Boş satıra yazılan `}`, tırnak içindeki ya da yorumdaki süslü parantezlere (örneğin `printf("{")`) değil,
  kendi `{` satırına hizalanır.

## 0.4 — 24 Ekim 2026

- Sağ üstteki ▶ düğmesi çerçevesiz, düz yeşil.
- Çökme mesajı iki satır: ne olduğu ve nedeni; 80 sütunluk konsolda artık bölünmez.
- README baştan yazıldı; yeni ekran görüntüleri: not programı, yazarken girinti, hatalar ve çökme.

## 0.3 — 24 Ekim 2026

- Süslü parantezsiz `if`/`else`/`for`/`while`/`do` sonrası otomatik girinti, tek komutun ardından geri dönüş;
  kendi satırına yazılan `{` üstündeki `if`/`else` ile hizalanır.
- Girinti düzeylerini gösteren soluk dikey çizgiler.
- Metinlerdeki kaçış dizileri (`\n`, `\t`, `\"`, `\\`…) ayrı renkte; anahtar kelimeler metinlerden daha
  kolay ayrılan bir renkte.
- Açıklamalar düzeltmeyi uygulamaz, ne yazılması gerektiğini söyler (“;” yazılmalı.); tıklamak o yere götürür.

## 0.2 — 23 Ekim 2026

- Program çıktısı ayrı pencere yerine kodun altındaki panelde; derleme mesajları paneli içeriğine göre boyutlanır.
- ⌘1…⌘8 sekme seçer, ⌘9 son sekmeye gider; ⌘⇧T kapatılan sekmeyi yeniden açar.
- Durum çubuğunda yalnızca sütun gösterilir.
- `{` sonrası otomatik girinti; boş satıra yazılan `}` kendi `{` satırıyla hizalanır.
- Anahtar kelimeler ve türler kalın; ters eğik çizgiyle biten `//` yorumunun alt satırı da yorum renginde gösterilir.
- `\a` tam güçte bir uyarı sesi çalar.
- Sık derleyici hataları için yeni Türkçe açıklamalar.

## 0.1 — 22 Ekim 2026

İlk sürüm.
