# Görev: Programlama Diline Yeni Özellikler Eklemek

Geliştirdiğimiz programlama diline aşağıdaki özellikleri ekle. Dil LLVM IR'a derleniyor ve bir standart kütüphanesi var.

## Önce: Sorularımı Cevapla

Kod yazmaya başlamadan önce şu soruları açıkla ve onayımı bekle:

1. `*u8` tipi bir string'i nasıl tutuyor? Bu bir pointer mı?
2. Program string'in nerede bittiğini nasıl anlıyor? Sonunda null terminator (`\0`) mı var?
3. String'in içinde birden fazla null karakter olursa ne oluyor?

Ben "tamam" dedikten sonra aşağıdaki maddelere geç.

## Eklenecek Özellikler

Maddeler birbirine bağlı olduğu için bu sırayla uygula:

### 1. Fonksiyon Overloading
- Aynı isimde ama farklı parametre tiplerine sahip birden fazla fonksiyon tanımlanabilsin (C++'taki gibi).
  - Örnek: `foo(string, int)` ve `foo(int, string)` birlikte var olabilsin.
- LLVM IR'a nasıl indirgeneceğine (ör. name mangling) sen karar ver ve yaklaşımını açıkla.

### 2. Tip Takma Adları (typedef / using)
- Var olan bir tipe yeni bir isim verilebilsin. Örnek: `int` → `usize`, `u8` → `uint8`.
- Fonksiyonlara da kısa veya farklı isimler verilebilsin.
- Söz dizimini dilin mevcut yapısına uygun şekilde tasarla.

### 3. RAII (Constructor / Destructor)
- Struct ve class'lara constructor ve destructor ekle.
- Ownership ve otomatik bellek serbest bırakma (deallocation), C++'taki RAII mantığıyla aynı şekilde çalışsın.
- Mevcut `defer` özelliği aynen kalsın, RAII'ye ek olarak kullanılabilsin.

### 4. Generic Tipler (Template)
- Tipler parametre olarak alınabilsin. Örnek: `vector<int>`, `vector<string>`.

### 5. Standart Kütüphanede `string` Tipi
- `string` derleyiciye gömülü olmasın, standart kütüphanede bir class olarak tanımlansın (`std::string` gibi).
- İçeride `*u8` tabanlı bir buffer kullanabilir.
- Kendi metotları olsun (uzunluk, birleştirme, karşılaştırma vb.).
- `char` 1 byte olarak kalsın, mevcut haliyle uyumlu çalışsın.

### 6. Standart Kütüphanede `vector<T>`
- C++'taki `std::vector` ile aynı mantıkta ama daha basit bir versiyon olsun.
- Herhangi bir tipi tutabilsin: `int`, `string`, nesneler vb.
- Belleğini RAII ile yönetsin.

### 7. Tek Bir `print` Fonksiyonu
- Şu an birden fazla print fonksiyonu var. Bunları kaldır, yerine tek bir `print` olsun.
- `print`, overloading ile farklı tipleri desteklesin.
- İlk argüman bir format string olabilsin.

## Çalışma Şekli
- İşleri mümkün olduğunca farklı subagent'lara bölerek paralel yürüt.
- Aklına takılan bir soru veya belirsiz bir ayrıntı varsa, kodlamaya başlamadan önce bana sor.