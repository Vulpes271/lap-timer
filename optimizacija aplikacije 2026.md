# Izhodišča za optimizacijo aplikacije

Pregled: **26. 11. 2026**. Obseg: Android aplikacija v `race/`, lokalna baza, slike, povezava s timerjem, izvoz in Google Drive. Ugotovitve veljajo za trenutno delovno kodo, vključno z lokalnimi spremembami.

To je pregled kode in obstoječega testnega APK. Časi zagona, gladkost pomikanja, poraba RAM/baterije in velikost nameščene izdaje na telefonu še niso izmerjeni. Spodaj jasno ločujem ugotovljeno stanje, pričakovani učinek in predlagano preverjanje. Optimizacijski posegi v tem koraku še niso izvedeni; izvedena je pomoč ob vnosnih poljih.

## 1. Prednostni vrstni red

Najprej bi uredil **obdelavo slik, izvoz/obnovo zunaj glavne niti, končno distribucijsko različico in zanesljivost prenosov/kopij**. Nato bi omejil obseg poizvedb in preračunavanje zgodovine. Menjava ogrodja ali popoln prepis aplikacije za to nista potrebna.

| Prednost | Področje | Ugotovitev v trenutni kodi | Naslednji poseg |
|---|---|---|---|
| P1 | Fotografije / RAM | `UriImage` dekodira izvirno sliko brez ciljne velikosti. | Pomanjševanje ob nalaganju, predogledi in omejen slikovni predpomnilnik. |
| P1 | Odzivnost | JSON/CSV serializacija in zapis ter del obnove lahko tečejo na glavni niti. | Jasna razmejitev IO/CPU dela in napredovanje/preklic operacije. |
| P1 | Velikost izdaje | Release ima izklopljen R8; testni APK vsebuje štiri ABI. | Optimiziran podpisan release, AAB oziroma ločeni APK po ABI. |
| P1 | Drive / robustnost | Ni nastavljenih časovnih omejitev, pretočnega prenosa in zagotovljenega zapiranja povezav. | Časovne/velikostne meje, pretočni IO, preklic, omejeno ponavljanje. |
| P1 | Obnova / varnost | Sistem kopira vse preference, tudi Wi-Fi geslo; fotografije niso del ročnega backupa. | Natančna politika kopiranja, zaščita poverilnic, prenosljiv paket z mediji. |
| P1 | Zunanji podatki | Manjkajo nekatere omejitve velikosti uvoza in zaščita CSV besedila pred formulami. | Meje podatkov pred razčlenjevanjem, preverjanje vsebine in varen izvoz za preglednice. |
| P1 pred distribucijo | Združljivost | V analiziranem APK je 10 knjižnic za 64-bitni arhitekturi; statični pregled 16 KB poravnave je uspel. | Enako preveriti končni APK/AAB in opraviti dejanski zagon na 16 KB napravi. |
| P2 | Obsežna zgodovina | Danes/Garaža opazujeta celotne sezname; zgodovina se filtrira v Kotlinu. | Namenske SQL poizvedbe, lahki povzetki in straničenje. |
| P2 | Preračuni | Km/servis/meritve večkrat berejo iste zgodovinske podatke. | Enoten vhodni posnetek in izračun samo prizadetega obsega. |
| P2 | Življenjski cikel | Veliko `collectAsState`, pogosti zapisi osnutkov; lastništvo povezave je razpršeno. | Zbiranje Room tokov glede na vidnost, zadržano shranjevanje osnutkov, en upravljavec povezave. |
| P2 | Baza / migracije | Sheme se ne izvažajo; reference večinoma zagotavlja repozitorij. | Izvoz shem, celovitost referenc in testi migracij na realni bazi. |
| P2 | UX in dostopnost | Material 3 je prisoten, postavitve pa pretežno za telefon. | Velika pisava, TalkBack, rotacija, manjše okno in prilagodljiva postavitev. |
| P3 | Vzdrževanje | En velik ViewModel, združljivostne dvojne evidence in legacy KAPT. | Postopna delitev po funkcijah in ločeno preverjena posodobitev gradnje. |

P1 pomeni prednost pred širšo uporabo ali distribucijo; P2 je pomembno za več podatkov in stabilno vsakodnevno uporabo; P3 izboljšuje nadaljnji razvoj. Oznaka ni trditev o že izvedenem napadu ali izmerjeni napaki na telefonu.

## 2. Kaj konkretno pomenijo smernice iz leta 2026

Google je avgusta 2026 poudaril omejitve pomnilnika v Androidu 17: spremljati je treba dinamični pomnilnik in slike, ker preseganje omejitev lahko povzroči upočasnitev ali končanje procesa. Za aplikacijo zato štejeta tudi omejen RAM in pravilna obnova stanja po ponovnem zagonu. [Uradno pojasnilo omejitev pomnilnika](https://developer.android.com/blog/posts/preparing-your-app-for-broader-memory-limits).

Objava Google Play z dne 26. 8. 2026 napoveduje uveljavljanje pragov pomnilnika, slik in optimizacije DEX od februarja 2027. Navaja najmanj 25-odstotno pokritost optimizacije/zmanjševanja/obfuskacije kode. To **ni zahteva, da mora biti vsak APK za 25 % manjši**. Za objavo je treba preveriti veljavna merila v Play Console. [Uradna objava o kakovosti](https://developer.android.com/blog/posts/elevating-app-quality-reducing-memory-usage-and-improving-device-migration).

Projekt že cilja API 37. Sam visok `targetSdk` še ne dokazuje kakovosti: potrebni so preizkusi dovoljenj, sprememb življenjskega cikla, varnega ohranjanja podatkov in dejanske odzivnosti. [Osnovne smernice kakovosti](https://developer.android.com/docs/quality-guidelines/core-app-quality).

## 3. Velikost aplikacije: ugotovljena izhodiščna sestava

Analiziran je bil **obstoječi debug APK z dne 22. 11. 2026**, pred novo gradnjo pomoči. Njegova velikost je **87.884.407 bajtov**, približno 87,9 MB oziroma 83,8 MiB. To ni meritev prihodnje optimizirane izdaje ali dejansko zasedenega prostora po namestitvi.

| Del APK | Izvorna velikost vnosa | Velikost v ZIP/APK |
|---|---:|---:|
| Izvorne knjižnice `.so` | 61.458.704 B | 61.458.704 B |
| DEX koda | 74.091.720 B | 22.912.460 B |
| Assets | 2.367.691 B | 2.164.937 B |
| Viri | 1.088.293 B | 991.238 B |
| Ostalo | 276.102 B | 92.261 B |

Razlika do celotne datoteke vključuje strukturo ZIP, podpis in poravnavo. Native knjižnice so približno **70 % celotne datoteke**. Vključene so `arm64-v8a` (16,1 MB), `armeabi-v7a` (10,1 MB), `x86` (17,7 MB) in `x86_64` (17,6 MB). Med največjimi vnosi sta OCR `libmlkit_google_ocr_pipeline.so` in čitalnik kod `libbarhopper_v3.so`.

Dodaten statični pregled vseh **10 knjižnic `.so` za `arm64-v8a` in `x86_64`** v tem APK je potrdil 16 KB poravnavo segmentov ELF ter podatkov v ZIP. Nezdružljivih vnosov v tem pregledu ni bilo. To ne nadomesti zagona aplikacije in kamere/OCR na dejanskem 16 KB okolju.

### Predlagani ukrepi

1. V [gradnji aplikacije](race/app/build.gradle.kts) je pri release `isMinifyEnabled = false`. Za projektni AGP **9.1.1** vklopiti `isMinifyEnabled = true` in `isShrinkResources = true`; obdržati optimizacijska pravila. Novejša dokumentacija za AGP 9.3+ kaže drugačen DSL, zato ga ne prepisati neposredno v ta projekt. Nato preveriti skeniranje, Room, izvoz/obnovo in Drive v optimiziranem release. [Uradna navodila R8](https://developer.android.com/topic/performance/app-optimization/enable-app-optimization/).
2. Za Google Play izdelati AAB; za neposredno nalaganje izdelati primeren APK po ABI. Ne odstranjevati arhitektur na slepo, saj sta lahko pomembna starejši telefon ali emulator. Tako telefon ne prejme knjižnic za vse procesorje. [Zmanjševanje velikosti aplikacije](https://developer.android.com/topic/performance/reduce-apk-size).
3. `material-icons-extended` lahko povečuje neoptimizirano DEX kodo. Najprej izmeriti učinek R8; po potrebi izbrane ikone zamenjati z majhnimi lokalnimi vektorskimi viri.
4. ML Kit je namenoma vključen v APK, da skeniranje/OCR delujeta že prvič brez interneta. Prehod na prenos modela zmanjša začetno aplikacijo, doda pa odvisnost od priprave modela in omrežja. Za uporabo v boksih predlagam ohraniti zagotovljen offline scan; opcijski OCR presojati ločeno. [Različici modela črtnih kod](https://developers.google.com/ml-kit/vision/barcode-scanning/android), [različici OCR](https://developers.google.com/ml-kit/vision/text-recognition/v2/android).

Natančne končne velikosti in odstotka prihranka brez release gradnje in merjenja ni mogoče obljubiti.

## 4. Fotografije: RAM, odzivnost in prostor

**Dokaz v kodi:** [AppComponents.kt](race/app/src/main/java/com/example/trackdaylog/ui/components/AppComponents.kt), `UriImage`, okoli vrstice 376: `BitmapFactory.decodeStream(stream)` brez zahtevane velikosti. Dekodiranje je že na `Dispatchers.IO`, kar je dobro, vendar še vedno ustvari celotno sliko v pomnilniku. [TyreCodeInput.kt](race/app/src/main/java/com/example/trackdaylog/ui/components/TyreCodeInput.kt), `keepTyreImage`, izvirnik prekopira; klici iz rezultatov galerije/OCR lahko tečejo na glavni niti.

Fotografija 4000 × 3000 v 32-bitnem slikovnem pomnilniku zahteva približno 48 MB pred dodatnimi kopijami. To je izračun, ne meritev trenutnega telefona. Več slik v galeriji lahko zato pomeni precej več RAM kot nakazuje velikost JPEG datotek.

**Poseg:** slike nalagati glede na velikost prikaza, na primer s primerno nastavljeno slikovno knjižnico ali ciljnim dekodiranjem; omejiti pomnilniški predpomnilnik. Pripraviti majhne predoglede in večjo sliko nalagati šele ob odprtju. Kopiranje/obdelavo prenesti na IO, preveriti dimenzije in velikost vhodne datoteke. Ob shranjevanju določiti razumno ločljivost in kakovost glede na potrebo po pregledu obrabe. [Uradna navodila za učinkovito nalaganje slik](https://developer.android.com/topic/performance/graphics/load-bitmap).

Za prostor na telefonu dodati pregled porabe medijev in varno čiščenje neuspelih/opuščenih začasnih posnetkov. Pred brisanjem preveriti vse reference v bazi in osnutkih. Izvirnikov ne brisati samodejno, dokler ni določena politika kopij in kvalitete.

**Preverjanje:** galerija z 20 velikimi fotografijami, večkratno odpiranje/zapiranje, zasuk, prehod v ozadje in preizkus na telefonu s 4 GB RAM. Spremljati slikovni pomnilnik, vrh RAM in končanja procesa.

## 5. Glavna nit, izvoz in uvoz

**Dokaz:** [TrackDayViewModel.kt](race/app/src/main/java/com/example/trackdaylog/viewmodel/TrackDayViewModel.kt), `exportJson` okoli 491, `exportCsv` 504, `uploadBackupToDrive` 517 in `restoreDriveBackup` 544. Funkcije so `suspend`, vendar `exportManager.exportJson`, CSV serializacija in `importJson` pred obnovo niso vse ovite v ustrezen dispatcher. Kličejo jih korutine UI. `suspend` sam po sebi ne prestavi dela na drugo nit.

[ExportManager.kt](race/app/src/main/java/com/example/trackdaylog/export/ExportManager.kt) izdela celoten objekt JSON/besedilo oziroma seznam CSV vrstic in nato uporablja `writeText`. [DriveBackupManager.kt](race/app/src/main/java/com/example/trackdaylog/backup/DriveBackupManager.kt) pri uploadu prebere datoteko v niz, izgradi celoten multipart niz in še bajtno kopijo.

**Poseg:** zagotoviti, da so operacije varne za klic z glavne niti. Blokirni IO naj teče na IO dispatcherju, večje procesiranje na CPU dispatcherju; pri velikih kopijah uporabiti pretočni zapis/branje in paketno obdelavo. Ohraniti transakcijski posnetek baze, da izvoz ne meša različnih stanj. Dodati stanje napredovanja in preprečiti več sočasnih izvozov.

`runCatching` v delu ViewModela prestreže tudi `CancellationException`. Preklic naj se ponovno vrže; za prekinjen upload se ne sme prikazati običajna poslovna napaka ali uspeh. Timer v svoji prenosni zanki to že obravnava pravilno. [Priporočila Android arhitekture](https://developer.android.com/topic/architecture/recommendations).

**Preverjanje:** vsaj 10.000 voženj in večje varnostne kopije, StrictMode v debug različici, počasna shramba, prekinitev operacije in premalo prostora. Preveriti, da UI ostane odziven in napaka ne delno zamenja baze.

## 6. Baza, seznami in izračuni

### 6.1 Poizvedbe omejiti na prikazano vsebino

**Dokaz:** [PaddockDao.kt](race/app/src/main/java/com/example/trackdaylog/data/dao/PaddockDao.kt) ponuja celotna `allSessions()` in `allTyres()`. [TodayOverview.kt](race/app/src/main/java/com/example/trackdaylog/ui/screens/TodayOverview.kt) prebere vse vožnje in jih filtrira za izbranega udeleženca. [PaddockScreens.kt](race/app/src/main/java/com/example/trackdaylog/ui/screens/PaddockScreens.kt) za Garažo opazuje vožnje, servise, gume in dnevne obračune ter iz tega obnavlja povzetke.

**Poseg:** dodati poizvedbe za dogodek/udeleženca/motor in majhne projekcije za kartice. SQL naj vrne zadnjo vožnjo, najboljši čas, trenutno montažo ali agregat, kadar ne potrebujemo vsakega polja vsakega zapisa. Večjo zgodovino prikazovati po straneh. Indekse dodajati po dejanskih `WHERE/JOIN/ORDER BY` in `EXPLAIN QUERY PLAN`, ne na vsako polje. Room že podpira asinhrone DAO-je in opazljive poizvedbe. [Room asinhrone poizvedbe](https://developer.android.com/training/data-storage/room/async-queries).

### 6.2 Dolgi seznami naj se izrisujejo po potrebi

Danes, Garaža in nekateri inventarni zasloni uporabljajo `Column.verticalScroll` s `forEach`. Ta pristop sestavi vse elemente. Za zgodovino/vožnje/inventar uporabiti `LazyColumn` s stabilnim ključem po ID in po potrebi `contentType`; kratki obrazci lahko ostanejo navadne kolone. Filtriranje in sortiranje naj ne tečeta ob vsaki nepovezani rekompoziciji. [Priporočila Compose za zmogljivost](https://developer.android.com/develop/ui/compose/performance/bestpractices).

### 6.3 Omejiti ponovljene zgodovinske preračune

**Dokaz:** [DayLedger.kt](race/app/src/main/java/com/example/trackdaylog/data/repository/DayLedger.kt), `motorcycleKm` okoli 91, `tyreKmAt` 108 in `refreshMeasurements` 118, večkrat preberejo udeležence, dogodke in odseke. `refreshMeasurements` za vsako meritev znova pokliče zgodovinski izračun. [GarageSummary.kt](race/app/src/main/java/com/example/trackdaylog/data/repository/GarageSummary.kt) to uporablja po motorju in servisnem intervalu. [QuickTyreMount.kt](race/app/src/main/java/com/example/trackdaylog/data/repository/QuickTyreMount.kt), `rebuild`, ponovno pregleda montažno zgodovino in za vožnje bere obrabo.

**Poseg:** enkrat naložiti potreben vhodni posnetek, zgraditi mape po ID ter izračunati samo prizadete motorje, gume in časovni interval. Čiste izračune ločiti od IO. Če bodo meritve pokazale potrebo, dodati izpeljane povzetke s pravili invalidacije. Ohraniti transakcije in idempotentnost; pospešitev ne sme povzročiti dvojnih km.

**Preverjanje:** primerjati rezultat starega in novega izračuna za dva voznika na istem motorju, menjavo gum, popravek za nazaj, razveljavitev in ponoven zaključek. Poleg časa meriti število SQL poizvedb.

## 7. Življenjski cikel, osnutki in poraba baterije

- Za UI zbiranje Room in drugih aktivnih zunanjih tokov uporabiti življenjskemu ciklu prilagojeno zbiranje, npr. `collectAsStateWithLifecycle`. Obstoječi `stateIn(...WhileSubscribed(5000))` je dobra osnova. Lahkih tokov izbire v pomnilniku ni treba spreminjati brez razloga. [Priporočila arhitekture](https://developer.android.com/topic/architecture/recommendations).
- [SessionScreens.kt](race/app/src/main/java/com/example/trackdaylog/ui/screens/SessionScreens.kt) pri spremembah ustvarja velik JSON osnutka in ga zapisuje v preference. Uvesti omejeno združevanje sprememb, npr. kratek debounce, in izrecno shranitev ob zapustitvi/ustavitvi. Določiti dovoljeno izgubo zadnjega vnosa pri nenadnem končanju procesa. Za zanesljive osnutke je smiseln ločen model/shramba.
- [TrackDayViewModel.kt](race/app/src/main/java/com/example/trackdaylog/viewmodel/TrackDayViewModel.kt) pogosto pomni aktivne izbire; ne zapisovati iste vrednosti ponovno, če se ni spremenila.
- [AppNavigation.kt](race/app/src/main/java/com/example/trackdaylog/navigation/AppNavigation.kt) ustvari `TimingLinkViewModel` tudi za uporabnika brez timerja; ta ob nastanku bere in razčleni celoten arhiv. Večji arhiv nalagati šele ob uporabi funkcije oziroma po delih, da ne obremeni običajnega zagona ročnega dnevnika.
- [TimingLinkScreen.kt](race/app/src/main/java/com/example/trackdaylog/ui/screens/TimingLinkScreen.kt) že odklopi ob `ON_STOP`. Toda povezovanje se sproži tudi iz navigacijskega korena. Lastništvo centralizirati in preveriti hladni zagon neposredno na Danes, prehod v ozadje in vrnitev. Ne trdim, da vsi ti primeri trenutno puščajo povezavo.
- Petsekundno preverjanje timerja je razumno za kratek prenos v boksih. Po zaključenem prenosu upočasniti preverjanje ali ponuditi ročno osvežitev; ob izgubi povezave uporabiti omejen backoff. Preklic naj dejansko prekine tudi blokirni zahtevek, ne samo korutine po njegovem koncu.

## 8. Robustnost in varnost

### 8.1 Omrežje in Drive

[DriveBackupManager.kt](race/app/src/main/java/com/example/trackdaylog/backup/DriveBackupManager.kt) nima nastavljenih `connectTimeout`/`readTimeout`, omejitve velikosti odgovora ali `disconnect()` v `finally`. Seznam datotek tudi ne obravnava `nextPageToken`. Dodati časovne/velikostne meje, pravilno zapiranje, straničenje ter prepoznavna stanja brez povezave/401/429/5xx. Ponavljanje uploada po nejasnem izidu lahko naredi dve kopiji; retry mora upoštevati identiteto operacije.

Za Drive preveriti prehod s `AccountManager.blockingGetAuthToken` na **AuthorizationClient**, ohraniti omejen `drive.file` scope in zahtevati dostop šele ob uporabnikovi Drive akciji. Credential Manager je za prijavo; ni samostojna zamenjava avtorizacije Drive. [Uradna navodila avtorizacije](https://developer.android.com/identity/authorization).

Timer že omejuje HTTP na naslov `192.168.4.1`, zavrača preusmeritve in ima časovne ter velikostne meje. To ohraniti. WPA2 in lokalni naslov sama ne dokazujeta identitete izvornega merilnika. Za produkcijski protokol določiti parjenje/identiteto naprave in po potrebi preverjanje pristnosti odgovorov; HTTPS ali podpis sporočil zahteva usklajeno spremembo ESP32. Ne uvajati sprejemanja poljubnih certifikatov. [Varnostna priporočila Android](https://developer.android.com/privacy-and-security/security-best-practices).

### 8.2 Geslo in varnostne kopije

**Dokaz:** `TimingLinkViewModel` shrani geslo v `timing-link` preference. [backup_rules.xml](race/app/src/main/res/xml/backup_rules.xml) in [data_extraction_rules.xml](race/app/src/main/res/xml/data_extraction_rules.xml) vključujeta vse preference. To ne pomeni, da druge običajne aplikacije lahko prosto berejo zasebno shrambo; pomeni pa, da je treba izrecno določiti, ali se geslo sploh seli v kopiji.

**Poseg:** izločiti poverilnice in nepotrebne osnutke iz sistemskega backupa. Če geslo ostane trajno shranjeno, ga zaščititi z ustrezno uporabo ključa Android Keystore in predvideti primer izgubljenega ključa/obnove na drug telefon. DataStore sam po sebi ne pomeni šifriranja. Gumb za pozabljanje povezave naj omogoča tudi dejanski izbris shranjenega gesla. [Android Keystore](https://developer.android.com/privacy-and-security/keystore), [pravila Auto Backup](https://developer.android.com/identity/data/autobackup).

Ročni JSON/Drive backup ohranja URI fotografij, ne slikovnih datotek. Sistemska pravila vključujejo bazo/preference, ne pa mape `tyre_photos` in arhiva timerja. Pripraviti prenosljiv paket z mediji ter vključiti ali jasno izločiti še nepripisane čase. Uporabiti manifest datotek, omejitve velikosti in preverjanje celovitosti; pri razširjanju ZIP preprečiti poti zunaj ciljne mape. Obnova na nov telefon mora biti dejanski test, ne samo uvoz iste baze na istem telefonu.

### 8.3 Uvoz JSON, timer in CSV

- Obstoječa validacija referenc, količin in transakcijska obnova sta dobri. Dodati zgornje meje velikosti datoteke, števila zapisov/krogov, dolžine besedil in globine podatkov pred dragim razčlenjevanjem.
- `SnapshotValidation` preverja veliko referenc, vendar vseh družin ne enako podrobno. Dopolniti preverjanje voznikov, staršev fotografij/obrabe/servisov, datumov, vgrajenega `lapsJson` in skladnosti kataloškega `nameKey`. Tudi notranji JSON nizi potrebujejo preverjanje.
- Trenutni timer izpusti osvežitev enake identitete z enakim številom krogov. Za popravljene podatke dodati revizijo ali hash. Že potrjeni uvoz naj ima nadzorovan popravek, brez podvojitve vožnje in brez izgube ročnih tlakov/opomb.
- `csvEscape()` okoli vrstice 216 v [ExportManager.kt](race/app/src/main/java/com/example/trackdaylog/export/ExportManager.kt) ureja narekovaje in ločila, ne začetkov formul. Ime ali komentar `=1+1` se lahko v preglednici obravnava kot formula. Pripraviti izvoz, ki nezaupanja vredno besedilo varno zapiše kot besedilo; ohraniti številčne stolpce kot številke. Preizkusiti tudi CR/LF in ponovno shranjevanje v Excel/LibreOffice. [OWASP o CSV formulah](https://owasp.org/www-community/attacks/CSV_Injection).

### 8.4 Integriteta in migracije baze

[AppDatabase.kt](race/app/src/main/java/com/example/trackdaylog/data/database/AppDatabase.kt) ima `exportSchema = false`. Vključiti izvoz shem in jih hraniti ob kodi. Obstoječe ročne migracije in regresijske teste ohraniti; avtomatska migracija brez pregleda ni cilj. [Uradna navodila za migracije Room](https://developer.android.com/training/data-storage/room/migrating-db-versions).

Povezave med tabelami niso povsod deklarirane kot tuji ključi. Pred njihovo uvedbo urediti sentinel ID 0 za zalogo/menjavo brez dneva in pomen starega `trackDayId`. Kaskadnih izbrisov ne dodajati brez pregleda zgodovinskih posledic. Preverjati tudi sočasno shranjevanje in konflikt številke vožnje. Finančne vrednosti `Double` so uporabne za osnovni dnevnik; če bo potreben strog obračun, določiti decimalno predstavitev/zaokroževanje in valute.

## 9. Združljivost Androida, dostopnost in gradnja

- **API 37/lokalno omrežje:** manifest in preverjanje `ACCESS_LOCAL_NETWORK` že obstajata. Preveriti prvi vstop, zavrnitev, odvzem dovoljenja med uporabo in ponovno povezovanje. Testirati tudi starejše različice z dovoljenji za lokacijo/Wi-Fi. [Lokalno omrežje v Androidu 17](https://developer.android.com/privacy-and-security/local-network-permission).
- **16 KB pomnilniške strani:** statični pregled 10 knjižnic za 64-bitni arhitekturi v izhodiščnem APK je uspel. Po končni gradnji ponoviti preverjanje pakiranja ter na 16 KB okolju preizkusiti tudi kamero in OCR. Sam Kotlin v aplikaciji ne pomeni odsotnosti native odvisnosti; statična poravnava ne dokazuje vsega vedenja med izvajanjem. [Uradno preverjanje 16 KB](https://developer.android.com/guide/practices/page-sizes).
- **Dostopnost:** nova pomoč ima majhno 18 dp ikono v standardnem območju dotika IconButton, opis polja za bralnik zaslona in zapiranje oblačka. Na telefonu še preveriti TalkBack, 200-odstotno pisavo, fokus tipkovnice in polja v dveh stolpcih. Pomoč ne nadomesti jasne oznake, enote in napake ob neveljavnem vnosu. [Privzeta dostopnost Compose](https://developer.android.com/develop/ui/compose/accessibility/api-defaults).
- **Prilagodljivost:** preveriti rotacijo, zložljiv telefon, razdeljeno okno in sistemske robove/tipkovnico. Širše okno naj smiselno razporedi vsebino, ne samo raztegne polj. [Osnovna kakovost Android aplikacije](https://developer.android.com/docs/quality-guidelines/core-app-quality).
- **Gradnja:** projekt uporablja AGP 9.1.1 in Kotlin 2.3.21 z legacy KAPT. Prehod Room generiranja na ustrezno podprt KSP preveriti ločeno od funkcionalnih sprememb in izmeriti čas gradnje. To prvenstveno izboljšuje razvoj, ne zagotavlja hitrejše aplikacije na telefonu.
- **Odvisnosti:** zakleniti in preverjati različice, omogočiti preverjanje odvisnosti ter v CI izvajati build/lint/teste. Knjižnic ne posodabljati vseh hkrati samo zaradi novejših številk. Podpisni ključ in gesla za release naj ostanejo zunaj repozitorija.

## 10. Kaj je že dobro in mora ostati

- Room je skupni lokalni vir podatkov, zahtevna shranjevanja uporabljajo transakcije.
- Kilometri so ločeni na izvorne, usklajene in dnevni total; dva voznika istega motorja se ne štejeta dvakrat.
- Uvoz ima stabilno izvorno identiteto in preprečuje dvojnike.
- Timer uporablja IO dispatcher, časovne meje in omejen odgovor; preklic ne postane navadna napaka.
- Predpomnilnik timerja uporablja `AtomicFile`.
- Kamera uporablja zadnjo sliko, omeji sočasno obdelavo in sprošča vire; ročni vnos ostane na voljo.
- FileProvider ni izvožen in dopušča samo mapo fotografij; splošni nešifrirani HTTP je prepovedan z omejeno izjemo za timer.
- Ročni dnevnik deluje brez timerja/Drive. Optimizacija ne sme pokvariti prve uporabe brez interneta.
- Obstajajo regresijski testi za migracije, skupne motorje, km, montažo/razveljavitev, zalogo in uvoz.

## 11. Merila za preverjanje izboljšav

Najprej izmeriti izhodišče na podpisani/optimizirani release različici in enem določenem telefonu; rezultate debug različice hraniti ločeno. Dodati **Macrobenchmark** in aplikacijski **Baseline Profile** za zagon → Danes → vnos vožnje ter Garaža → motor → menjava gum. Učinka profila vnaprej ne izražati kot zagotovljen odstotek. [Baseline Profiles](https://developer.android.com/topic/performance/baselineprofiles/overview).

| Scenarij | Kaj meriti | Pogoj sprejema |
|---|---|---|
| Hladen/topel zagon | Čas do prvega in uporabnega zaslona, RAM | Rezultat boljši ali enak izhodišču, brez dodatnega čakanja na neuporabljen timer/Drive. |
| 10.000 zgodovinskih voženj | Čas odprtja Danes/Zgodovine, SQL poizvedbe, RAM | Prikazani obseg ne zahteva nalaganja vseh podrobnosti zgodovine. |
| Pomikanje in tipkanje | Izpuščene sličice, trajanje sličic, IO na glavni niti | Brez blokirnega IO; brez vidnih zastojev pri običajnem vnosu. |
| 20 velikih fotografij | Slikovni RAM, vrh RAM, rast po ponovnih odprtjih | Slike se dekodirajo za prikaz; ni naraščanja brez meje ali končanja procesa. |
| Prenos 8 in več krogov | Čas, velikost podatkov, izguba Wi-Fi, ponovitev | En ciljni zapis, brez podvojenih km in z ohranjenimi ročnimi podatki. |
| Izvoz/obnova velike baze | RAM, odzivnost, celovitost, preklic | UI odziven, napaka ne pusti delne obnove. |
| Premalo prostora / pokvarjen uvoz | Povrnitev, razumljivo obvestilo | Stari podatki ostanejo, ponovno je mogoče poskusiti. |
| Ponoven zagon/končanje procesa | Osnutek, aktivni dan, importi, geslo | Obnovljeno ali izrecno označeno stanje; brez tihe izgube potrjenih podatkov. |
| Nov telefon | Baza, fotografije, še nepripisani časi, prijava/povezava | Kopija ne vsebuje nedostopnih referenc brez opozorila; poverilnice imajo določeno politiko. |
| Končna distribucija | Download, nameščen prostor, native poravnava | Izmerjena velikost za ciljni ABI in uspešen 16 KB preizkus. |

Številčne cilje za čas/RAM določiti po meritvi na izbranem telefonu. Velikost aplikacije na disku, RAM med uporabo in promet pri prenosu so tri različne metrike.

## 12. Predlagani izvedbeni sklopi

1. **Varnost podatkov in odzivnost:** IO/preklic izvozov, Drive časovne meje in pretočni prenos, velikostne meje, varen CSV ter politika backupov/gesla.
2. **Slike in distribucija:** predogledi/pomanjševanje, omejen predpomnilnik, R8/shrink resources, izmerjen APK po ABI/AAB in 16 KB preverjanje.
3. **Več podatkov:** namenske DAO poizvedbe, lazy seznami, življenjskemu ciklu prilagojeno zbiranje in manj ponovljenih preračunov.
4. **Trajna kakovost:** sheme/migracije, meritve in Baseline Profile, fizični preizkusi, nato postopna poenostavitev modela/ViewModela in gradnje.

Vsak sklop naj ima ločeno preverljiv rezultat in primerjavo z izhodiščem. Najprej ohraniti pravilnost zgodovine, montaž, zaloge in finančnih podatkov; šele nato oceniti hitrostni prihranek.

## 13. Kaj je bilo preverjeno ob tej spremembi

Po dodajanju pomoči je 26. 11. 2026 uspešno zaključen `:app:testDebugUnitTest :app:assembleDebug :app:lintDebug`: **89 testov v 12 paketih**, brez neuspehov, napak ali preskočenih testov. Lint je zaključil z **0 napakami, 37 opozorili in 5 namigi**. Štirje novi UI testi preverjajo odpiranje/zapiranje pomoči, ohranitev vnosa, SL/EN, oko pri geslu ter neodvisnost predlogov in pomoči. Uporabljen je [aktualni Compose testni API v2](https://developer.android.com/develop/ui/compose/testing/migrate-v2).

Novi **debug** APK ima **88.814.476 B**; primerjava sestave v 3. poglavju ostaja izhodiščni APK z dne 22. 11. Nova številka ni meritev optimiziranega release. Testne knjižnice so omejene na test/debug konfiguracijo. Statični pregled poravnave ELF/ZIP je uspel tudi na novem APK za vseh 10 knjižnic za 64-bitni arhitekturi.

Izrisanih in posodobljenih je 11 predogledov; vizualno sta pregledana vnos vožnje in povezava s timerjem. Preizkus nove pomoči na fizičnem telefonu, dejanske meritve hitrosti/RAM/baterije in ukrepi iz optimizacijskega načrta še niso izvedeni. Samodejni funkcionalni testi ne predstavljajo meritev zmogljivosti ali celovitega varnostnega preizkusa.
