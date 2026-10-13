## Faz 2 — Tip sistemi ve bellek düzeni (layout)

- [ ] **2.1** Tip modeli: boyut/hizalama (`size_of`, `align_of`) hedefe (`DataLayout`/pointer genişliği) göre hesaplanır.
- [ ] **2.2** İşaretli/işaretsiz semantik: `sdiv/udiv`, `srem/urem`, `sext/zext`, `icmp slt/ult`,
  `ashr/lshr` — tip bazlı seçim. Örtük dönüşüm kuralları sema'da (tek yerde). Sıfıra bölme kayıtlı
  `panic` rolünü çağırır (kayıtlı yoksa trap).
- [ ] **2.3** Layout motoru (yeni `src/sema/layout.cpp`):
  - `@repr(c)`: alanlar bildirim sırasında, C kurallarıyla hizalama/padding, `sizeof` = C `sizeof`.
  - varsayılan: derleyici serbestçe sıralayabilir (ör. büyükten küçüğe) — spesifikasyon verilmez.
  - `@packed`, `@align(N)` desteği.
  - Üretilen LLVM struct tipleri, hesaplanan offset'lerle birebir uyuşur (gerekirse açık padding alanı).
- [ ] **2.3b** Düzen şemaları ve geçmiş (`src/sema/layout_history.cpp`):
  - Varsayılan düzen, sürümlü ve deterministik **şema** (v1, v2, v3...); eski şemalar donmuştur ve
    compiler'da kalır, yeni davranış yeni şema olur. `--layout-scheme=`, `@layout(scheme = vN)`.
  - Dışa aktarılan tipler için **düzen kaydı** (şema kimliği, alan sırası, offset, boyut, hizalama,
    karıştırıldı/seed bayrağı) `.o`/`.a`/`.bc`/`.ll` içine metaveri olarak yazılır (LLVM named metadata
    `!xl.layout`, nesnede ayrı bölüm).
  - Tüketici tarafta (`declare` veya xl nesnesi) kayıt okunur; `xl.layout` proje geçmişi de aynı biçimde.
    Karıştırılmamış düzen şema + tip tanımından yeniden türetilir; `@layout(shuffle)` kayıtta işaretlenir.
  - Kayıt yok ve `@repr(c)` yok → C varsayımı + `W0201`; kayıtlı düzenle çelişki → `W0202`;
    şema uyuşmazlığı → `W0203`.
  - CLI: `--layout-history=`, `--layout-record`, `--print-layout <Tip>`.
- [ ] **2.4** Derleme zamanı sorguları: `size_of(T)`, `align_of(T)`, `offset_of(T, alan)`; `static_assert(cond, "msg")`.
  C başlığıyla layout doğrulaması bunlarla yapılır.
- [ ] **2.5** Sabit boyutlu dizi tipi `[T; N]` (C struct uyumu için): `N` derleme zamanı sabit ifadesi
  (`define` sabiti olabilir); `size_of([T; N]) = N * size_of(T)`, hizalama `align_of(T)`; yerel, struct/class
  alanı ve global olarak kullanılabilir; `@repr(c)` struct içinde C'deki `T[N]` ile aynı düzen.
  - **Değer tipi:** atama, parametre ve dönüşte tamamı kopyalanır; kopyayı önlemek için `&a`.
  - **Literal:** `[a, b, c]` ve tekrar biçimi `[v; N]`; eleman tipi bağlamdan çıkarılır.
  - **İndeksleme `a[i]`:** dizi ve `*T` üzerinde; indeks `usize` (aynı işaretlilikte genişleme örtük,
    işaretliler `as usize`). `*T` indekslemesi `*(p + i * size_of(T))` ve **kontrolsüzdür**. Dizi
    indekslemesi `--safety=checked` (varsayılan) altında sınır kontrolü yapar, dışındaysa kayıtlı `panic`'i
    çağırır; `unchecked` ya da `@unchecked fn` kontrolü kaldırır. Sabit indeks sınır dışıysa derleme hatası.
  - Global başlangıç değerleri ve kuralları plan/eg/ §4 "Global değişkenler"de.
- [ ] **2.6** `enum Ad: T { Üye [= değer], ... }`: `T` derleme zamanı sabiti olabilen herhangi bir tip
  (tamsayılar, `f32/f64`, `bool`, `*ui8` string literal'i); `T` yazılmazsa `i32`. Üyeler `Ad.Üye` ile
  erişilir. Otomatik numaralandırma yalnızca tamsayı alt tipte (önceki + 1, ilk üye 0); diğer alt tiplerde
  her üyenin değeri yazılmalıdır. `Ad` ayrı bir tiptir: alt tipe/alt tipten dönüşüm açık `as`; `==`/`!=`
  aynı enum'da geçerli. Değerler derleme zamanı sabitidir (`static_assert`, global başlangıç değeri,
  atomic `order` argümanı için kullanılabilir). Layout alt tipin düzenidir; `export(c)` imzasında yalnızca
  tamsayı alt tipli enum kabul edilir. `MemoryOrder` (6.1) bu mekanizmayla tanımlı builtin enum'dur.

**Kabul:** `@repr(c)` struct'lar için clang'ın ürettiği `sizeof/alignof/offsetof` ile test
(birkaç karma tipli struct, iç içe, packed, aligned) birebir uyuşur; x86_64 ve aarch64 için.
