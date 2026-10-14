## 0. Temel kurallar (her şeyden önce)

1. **Geriye ve ileriye uyumluluk YOK.** Compiler hiç kullanılmadı. Sözdizimi, AST, CLI, IR,
   dosya yapısı, her şey kökten değiştirilebilir. "Eski davranışı koru" diye bir gereksinim yok.
2. **`examples/`, `test/xlang/`, stdlib/runtime `.xlang` dosyaları tek kullanımlık** (yeni dilin
   kaynak uzantısı **`.xl`**; eski `.xlang` dosyaları yeni compiler'a uyarlanmaz). Kırılabilir,
   silinebilir. Compiler hedeflendiği gibi çalıştıktan sonra yeni dil için yeniden yazılır.
   Bu dosyalar **okunmaz**, tasarıma girdi olmaz, bir şeyi onlara uydurmak için çalışılmaz.
3. **`.cursor/` altındaki kurallar/planlar okunmaz.** Tek girdi kullanıcının yönergeleridir.
4. **Compiler yalnızca dili, CPU primitiflerini, builtin listesini ve `register` ile kayıtlı rolleri
   (`panic`, `allocator`) bilir.** Thread, scheduler, print kavramı compiler'da yoktur; bunlar xlang
   stdlib'dir. Builtin fonksiyon = **gövdesini compiler'ın kendisinin sağladığı** fonksiyon (liste kural
   7'de). `memcpy/memset/memmove/memcmp`, `malloc`/`free` ve allocator builtin değildir, stdlib'dendir;
   `new` allocator'ı kayıtlı `register(allocator)` global'inden ya da açıkça verilen `a`'dan alır
   (4.11, 4.12). Compiler doğrudan destekleyemeyeceği bir şeyi builtin yapmaz; o şey stdlib'e gider.
   **Compiler stdlib içindeki tip/fonksiyonlara özel muamele yapmaz**: onları ada ya da yola göre tanımaz,
   stdlib sıradan xl kodudur, `register` her modülde aynı çalışır; `panic`/allocator gibi roller yalnızca
   `register` kaydıyla bağlanır. Compiler varsayılan stdlib'i **yükler** (bu sürücü seviyesinde bir karardır:
   `--no-std` ya da `--std=<yol>` ile kapatılır/değiştirilir) ve yüklenen stdlib'in kayıtları diğer her
   modülünki gibi sayılır.
5. **Önce compiler doğru çalışsın.** Stdlib/example işleri compiler hedefine ulaştıktan sonra gelir.
6. **Plan ve dil şeması senkron kalır:** dil seviyesini etkileyen her plan değişikliği
   `plan/eg/`'yi de günceller (bkz. `CLAUDE.md`).

7. **Global isim uzayı kapalıdır ve iki katmanı vardır.** Kaynak koduna "bedavaya" (import'suz) gelen
   isimler yalnızca (a) compiler'ın bildiği builtin'ler ve (b) bir modülde `register` ile kaydedilmiş
   semboller (4.12) olabilir. Builtin listesi (kesin hali Faz 6'da netleşir):

   | Grup | Adlar | Yazım |
   |------|-------|-------|
   | Syscall | `syscall` | öneksiz, doğrudan `syscall` |
   | Atomic | `__builtin_atomicLoad/Store/Xchg/Cas/Add`, `__builtin_fence` | `__builtin_` önekli |
   | Inline asm | `__builtin_asm` | `__builtin_` önekli |
   | Memory order | `MemoryOrder` (enum tipi: `MemoryOrder.relaxed`, `.acquire`, `.release`, `.acqRel`, `.seqCst`) | öneksiz tip |
   | Derleme zamanı | `sizeOf`, `alignOf`, `offsetOf`, `staticAssert` | öneksiz |
   | Sabit | `nil` (boş pointer; her `*T`'ye atanır, `0 as *T` ile aynı) | öneksiz |

   Builtin fonksiyonlar iki biçimde yazılır ve **hepsi `__builtin_` önekini taşımaz**.
   `register` ile kaydedilenler (ör. stdlib'in `panic`'i) builtin değildir: compiler onları stdlib'den
   değil kayıttan tanır, `register` hangi modülde olursa olsun aynı çalışır.
   `print`, `alloc`, `memcpy`, `malloc`/`free`, `assert` gibi hiçbiri builtin de `register`'lı da değilse
   global değildir; stdlib'den `import` ile gelir.
   **libc yasaktır:** hiçbir yerde kullanılmaz, çağrılmaz, taklit edilmez.
   `declare` yalnızca kullanıcının kendi bağladığı bir nesneden (ör. `api.o`) gelen dış şeyler içindir
   (fonksiyon, global, struct, class, interface, type); xl modülleri arası erişim `import`/`export` iledir.
