## Sıra

```
Faz 0 ─► Faz 1 ─► Faz 2 ─► Faz 3 ─► Faz 4 ─► Faz 5 ─► Faz 6 ─► Faz 7 ─► Faz 8
 güvenlik  syntax   tipler    #if      sema/    codegen  primitif  stdlib   ölçek
                    layout    CLI      HIR      kalite
```

- Geriye uyumluluk olmadığı için ara geçiş katmanı yazılmaz; her faz kendi yüzeyini doğrudan değiştirir.
- Faz 5.1–5.3 küçük ve bağımsız; Faz 0'dan sonra herhangi bir noktada öne çekilebilir.
- Faz 2 (layout) ve Faz 4.4–4.5 (class/interface) birbirine bağlı: class düzeni Faz 2 motorunu kullanır.
