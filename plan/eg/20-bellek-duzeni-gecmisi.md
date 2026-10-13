## 20. Bellek düzeni geçmişi

xl'in varsayılan (xlang) düzeni **sürümlü ve deterministik** bir algoritmadır; compiler geçmişteki
algoritmaların hepsini içinde taşır ve eski bir nesnenin hangi düzenle üretildiğini bilir.
Düzen, derleyici sürümü değiştiğinde sessizce kaymaz.

```
düzen şeması   v1  →  v2  →  v3  (güncel)         Her şema donmuştur; yeni davranış yeni şema olur.
                                                   Eski bir .o "v2 ile üretildi" der; compiler v2'yi bilir.
```

| Konu | Davranış |
|------|----------|
| Şema kimliği | Her xl nesnesi/başlığı/IR'ı kullandığı düzen şemasını taşır (`xl.layout` metaverisi) |
| Düzen kaydı | Dışa aktarılan her tip için alan sırası, offset, boyut, hizalama yazılır |
| Bilerek karıştırma | `@layout(shuffle)` veya sertleştirme bayrağı; kayıt "karıştırıldı + seed" olarak işaretlenir |
| Karıştırılmamışsa | Compiler, kaydı okumadan şema + tip tanımından düzeni **yeniden türetir** |
| Proje geçmişi | `xl.layout` dosyası projedeki tiplerin son bilinen düzenini tutar; derleme bunu kullanır |
| Statik kütüphane / `.o` | Düzen kaydı nesneyle gelir; xl'in kendi stdlib'i kod yerine `.a`/`.o` olarak gelse bile çalışır |
| Başka dillerden nesne | Kayıt yok; `@repr(c)` ya da C varsayımı + uyarı (`W0201`) |

```xlang
// libgeom.a, xl ile derlenmiş ve düzen kaydı taşıyor (şema v3)
declare struct Vec3 { x: f32, y: f32, z: f32 }   // kayıttan okunur, @repr(c) gerekmez

@layout(scheme = v3)                              // şemayı sabitler (yeni şema çıksa bile kayma olmaz)
struct Packet { id: ui16, flag: bool, len: ui64 }
```

| CLI | Anlam |
|-----|-------|
| `--layout-scheme=v3` | Varsayılan düzen şemasını sabitle |
| `--layout-history=<dosya>` | Düzen geçmişi dosyası (varsayılan `xl.layout`) |
| `--layout-record` | Derlemede düzen kaydını (yeniden) yaz |
| `--print-layout <Tip>` | Bir tipin düzenini (offset/boyut/hizalama) yazdır |
