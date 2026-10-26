İkinci isteğim de şu. İkinci isteğim de kanka print çok fazla print var. Print bir tane olsun. Tamam mı? Ve şöyle olsun. Hani C++'da
  bir den fazla aynı fonksiyon farklı argüman prototipiyle bulunabiliyor ya. Örnek veriyorum string ilk parametre string, ikinci
  parametre int veyahut da ilk parametre int, ikinci parametre string ile aynı isimdeki fonksiyon oluşabiliyor ya. Ya bunun tam
  kuralını ben bilmiyorum. C++'daki ama sen neyi kastettim anladın kanka. Yani ben tam olarak bunu istemiyorum ama şöyle düşün. Aynı
  farklı sembollerde iki aynı fonksiyon olabilir. Bunu artık aşağıda LLVM ayara IRE'ye nasıl indirgersin bilmiyorum. Ama bu bu şekilde
  olmalı. Ve print de buna göre beslenmeli. Örnek veriyorum print'in ilk argümanı format string olabilir. Anladın mı eksilimi?
  Anladın mı demek istediğimi? İkinci olarak standart library'nin içinde string olmalı. Yani std string gibi düşünebilirsin. Bu bir
  tip. UI8 gibi. Üçüncü isteğim ise type definition vardı ya C++'da. Örnek veriyorum, type definition ile custom tipler oluşturup
  onların arkasında bir tip verebiliyorduk. Neydi? Bu type definition mu? Emin değilim ama örnek veriyorum işte int'i u size gibi
  gösterebiliyorduk. Anlatabildim mi? Ya da ne bileyim u8'i uintptrt8 gibi gösterebiliyorduk. Ya da herhangi bir fonksiyonu farklı
  isimle tanımlayabiliyorduk. Anladın mı? Kısaltma olsun, kolay olsun gibi. Ya anladın demek istediğimi? Şey yap. Yıldız u8'i arkada
  string olarak göster ve işlemleri string olarak yap. Ve string'in kendi fonksiyonları olsun. Anladın mı? String bir class olsun. O
  class içinde u i8 Ya da emin değilim kanka ya. Kafam çok karıştı. Sen dediğim şu üç şeyi ekle. O type definition mantığını
  benim dilime göre ekle. İkincisi string tipi ekle ama bu string compiler level olmasın. Standart library içinde olsun. Ve type
  templating ekle. Örnek veriyorum, standart library içinde vektör olsun. Vektör iki tane açılan kapanan üçgen içinde tip belirsin.
  Örnek veriyorum, bir obje, string, int vesaire. Vektörün içinde de işte klasik C++ vektörüyle aynı olacak mantık ama daha basit
  olacak, tamam mı? Bunu yap. Yani eklemen, şu deliklerin hepsini ekle kanka, organize et. Aklında bir soru varsa yaz. En ufak bir
  ayrıntı varsa yaz bana. Örnek veriyorum. Mesela ben şunu bilmiyorum kanka. İlk önce şimdi soracağım soruyu bana cevap ver. Sonra
  tamam dedikten sonra işte bu dediklerimi yapmaya başla, tamam mı? Sorum şu. Yıldız UI8, yıldız UI8 nasıl string olduğunu anlıyor
  yazılım. Yani oraya string nasıl koyabiliyoruz? Ben onu anlamadım. Hani o mesela string'in nerede bittiğini nasıl anlıyor? Bu bir
  pointer mı? Bu ne bu? Hani string'in sonunda ne var? Null termination mu var? String içinde birden fazla null termination olunca ne
  olacak? Anladın mı? Bu nasıl string tutabiliyor? Onu ben anlamadım. Kanka ikinci olarak da Kanka neden char tuttuğunu anladım. Okey.
  Char 1 byte olduğu için Jones char yıldızdaki C++'ı karşılığı olarak tutmuşsun. Eyvallah. Aynı şekilde setete string eklemeni
  istiyorum. Ve ek olarak da deconstruction ve construction eklemeni istiyorum. Yani ray. R-A-I-I. C++'daki ray mantığını eklemek
  istiyorum. İşte ownership ve deallocation mantığını eklemeni istiyorum. Struct'larda ya da class'larda bu deobstruct, deconstruction
  olmak zorunda. Veya C++'da nasıl çalışıyorsa aynı mantık bu programlama dilinde de geçerli. Bunları mümkün oldukça farklı
  subagent'lara bölerek yaptırdık. Yaptırdık değil. Bu arada dictation hataları var, onlara takılma. Zamanında defer eklemiştik. Defer
  aynen kalsın kanka. Defer ekstra. Ekstra doldur doldur diye bir şarkı var ya, aynı onun gibi ekstra.