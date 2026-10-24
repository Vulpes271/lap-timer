# ESP32 lap timer

Diagnostika GPS NEO-6, MPU6500/9250/9255 in SD kartice na ESP32-WROOM-32.
Wi-Fi test je odstranjen.

## Prva verzija: povezava GPS, IMU in zaslona

Prva integrirana verzija podpira ESP32, NEO-6 pri 5 Hz, MPU6500/9250/9255,
SPI TFT 480x320 in preverjen SD CSV zapis. Trenutni nacin je demonstracijski:
cas kroga 0:00.0, LAP 0, tlaka in baterija brez senzorjev, zapis SD izklopljen.
Po zahtevi je ob zagonu vklopljen ZELENI PREIZKUS: vseh pet segmentov ob obeh
robovih je zelenih, oznaka TEST jasno loci prikaz od dejanske primerjave kroga.
Ukaz g preizkus izklopi/vklopi. Napisa LEVO in DESNO sta odstranjena.

### Osnova SETUP menija in nicla nagiba

Serijski ukaz s odpre SETUP; ponovni s ga zapre na glavni zaslon. V SETUP
ukaz n vrne neposredno na glavni zaslon, drugod preklopi na naslednjega.
Ukazi q, b in Escape vedno vrnejo na glavni zaslon. Terminal ob preklopu
izpise ZASLON: GLAVNI / SETUP / DIAGNOSTIKA / MIRUJOCI TEST, ob n tudi UKAZ: N.
Pri poravnanem in mirujocem motorju z shrani trenutno staticno
oceno nagiba kot niclo. Preveri se svez MPU podatek, norma blizu 1 g in majhna
kotna hitrost; to je osnovni pogoj, ne zanesljiv detektor mirovanja.
Ukaz c nastavi niclo nazaj na tovarniskih 0 stopinj. Nicle se ne zajame samodejno.
Odmik je shranjen v NVS ESP32 prek Preferences in ostane po izklopu napajanja.
Zeleni preizkus je zacasen in je ob vsakem zagonu znova vklopljen za ta demo.
Senzorska fuzija in fizicni gumb se nista implementirana. V meniju so trenutni
nagib, shranjena nicla in stanje preizkusa; isti ukazi bodo lahko povezani z gumbom.

## Trenutni zacetni zaslon: demonstracija casa kroga

Ob zagonu se za 0,8 s prikaze napis RACE, nato se odpre prvi zaslon:

- Na sredini je velik cas M:SS.d; trenutno je vedno 0:00.0.
  Pod 10 minutami se uporablja trikratna velikost pisave 7 (456 x 144 pik
  za prikaz M:SS.d); ob morebitnem daljsem krogu se pisava zmanjsa, cas se ne ovije.
  Zaznavanje piste, startno-ciljne crte in samodejni zacetek merjenja se niso
  implementirani; demonstracija ne meri casa od vklopa naprave.
- Nad casom je manjsi LAP 0 (stevilka trenutnega kroga; pred voznjo 0).
  Levo in desno ob casu je prostor za petsegmentni primerjalni indikator.
  Negativna delta proti najboljsemu krogu na isti razdalji obarva indikator
  zeleno, pozitivna rdece, nicelna je nevtralna. Vsak segment predstavlja
  zacetih 0,2 s razlike, najvec pet segmentov. Zraven LAP je izpis BEST z delto
  v sekundah. Ko je zeleni preizkus izklopljen in ni reference, je BEST -- in indikator siv.
  Za zdaj so lapNumber=0, bestDeltaValid=false; izracun krogov, najhitrejsega
  kroga in primerjave po razdalji se ni implementiran. To je demonstracija UI,
  ne samodejno stetje krogov ali fizicne LED diode.
- Zgoraj je majhna skupna vrstica GPS n, BAT n% in SD n%.
  GPS prikaze stevilo satelitov v uporabi iz GGA, brez svezega podatka --.
  BAT ostaja --, dokler ni priklopljena in umerjena meritev baterije.
  SD prikaze odstotek zasedenosti datotecnega sistema (zaokrozen navzdol),
  preverjen ob zagonu; trenutno se ne spreminja, ker je zapis izklopljen.
  Branje zasedenosti se ne izvaja med izrisom, da ne blokira zajema GPS.
  Pod vrstico je levo R (tlak ZADAJ), desno F (tlak SPREDAJ), vsak v eni
  vrstici skupaj z vrednostjo v bar. Brez senzorjev: R --.-- bar in F --.-- bar.
  Bluetooth senzorji in simulirane vrednosti niso vkljuceni.
- Spodaj na sredini je stevilcna vrednost nagiba, na obeh straneh pa prostor
  za locena polnjena trakova (-60 do +60 stopinj, najvec 125 pik na stran).
  Sivega okvira/ozadja, crtic merila in napisov ni. Trak raste od notranjega
  roba ob stevilki navzven proti smeri nagiba.
  Pri spremembi se doda ali odstrani samo razlika med prejsnjo in novo dolzino;
  celoten trak se ne brise. Pri prehodu cez niclo se odstrani stara
  stran in napolni nova, nespremenjen kazalnik pa se ne izrisuje znova.
  Stevilka je absolutna vrednost stopinj brez oznak L/D; smer pokaze pravokotnik.
  Stevilka prikaze tudi vrednosti zunaj tega obmocja, kazalnik pa se omeji
  na konec merila. Brez svezega MPU podatka se izpise -- deg.

Trenutni nagib je staticna ocena atan2(ay, az), zmanjsana za shranjeno niclo
in ovita na -180 do +180 stopinj. Brez nastavitve je nicla ay=0 in az>0;
predznak in os sta odvisna od namestitve MPU.
To ni kalibriran kot nagiba motorja med voznjo: linearni pospeski in zavoji
vplivajo na pospeskomer. Nastavitev nicle namestitve je v SETUP; senzorska
fuzija sledi kasneje. Demonstracijski cas in tlaki so v DashboardData loceni od izrisa.

DashboardScreen ima zaslone LapTimer, Diagnostics, Stationary in Settings. Privzeti je
LapTimer; nextScreen() preklopi na naslednjega. Serijski ukaz n za zdaj omogoca
preklop brez gumba. GPIO in logika fizicnega stikala se nista dodana. Ob preklopu
se narise postavitev, med delovanjem samo spremenjene regije (ena na 25 ms).
Na prvem zaslonu je sedem regij, zato celoten krog traja najmanj 175 ms.

SD zapisovanje ostaja IZKLOPLJENO, ker je STATIONARY_TEST=true in statistika
ustavljena. Kartica se ob zagonu samo preveri z branjem. Sprememba zaslona
ne vklopi belezenja ali merjenja. Spodnji opis CSV velja za nacin belezenja:
nova /gpsNNNN.csv ob vsakem zagonu, pri vsaki GPS GGA epohi (5 Hz) nova vrstica
GPS in MPU podatkov, nato zaprtje ter primerjava s ponovno prebrano vrstico.
Ob napaki se zapis ustavi; obstojece datoteke se ne prepisujejo. Trenutna CSV
ne vsebuje tlakov, kalibriranega nagiba ali rezultatov krogov.

## Trenutni nacin: mirujoci preizkus, samo prikaz

V src/main.cpp je STATIONARY_TEST=true. V tem nacinu je ustvarjanje CSV in
zapisovanje na SD izklopljeno; obstojeca diagnostika kartice samo preveri branje.
Za prejsnji prikaz in CSV belezenje nastavi STATIONARY_TEST=false.

MPU se bere z nominalnim intervalom 20 ms (50 Hz, dejanski interval je odvisen
od trajanja izrisa in I2C); GPS ostane na 5 Hz. Statistika se zacne po 10 s
umirjanja po ukazu r. Trenutno je test ob zagonu USTAVLJEN na zahtevo uporabnika.
Anteno in senzor postavi na stalno mesto, nato poslji serijski ukaz r in pusti
mirovati nekaj minut. Ukaz p zamrzne statistiko; sveze meritve se se prikazujejo.
RESET pusti test ustavljen in izgubi statistiko iz RAM-a. Premik med meritvijo
onesnazi statistiko. Serijski ukaz r ponastavi statistiko brez ponovnega zagona
GPS-a in uvede 10 s umirjanja. Stevci GPS komunikacije ostanejo od zagona.

Program hrani samo skupne statisticne akumulatorje, ne zgodovine po casovnih
oknih. Zadnjih treh minut iz ze zbranega povprecja/raztrosa zato ni mogoce
odstraniti. Pred ustavitvijo je bil surov skupni povzetek (s premikanjem vred)
zajet v tools/latest_stationary_snapshot.log in ne predstavlja cistega
mirujocega rezultata. Prejsnji 44-sekundni zajem je imel GPS raztros 0,91 m;
to je rezultat tistega kratkega odseka, ne celotnega daljsega testa.

Zaslon prikaze:

- GPS stanje, stevilo satelitov in HDOP (brez enote, ni napaka v metrih).
- Trenutno, povprecno in najvecjo GPS hitrost; pri mirovanju je odstopanje od
  nic navidezno gibanje. Vzorce hitrosti in polozaja steje loceno.
- Trenutni in najvecji odmik od prvega veljavnega polozaja ter raztros okoli
  povprecja: sqrt(sigma_vzhod^2 + sigma_sever^2), v metrih. To ni absolutna
  tocnost, CEP ali zagotovljen polmer napake. Za absolutno tocnost je potrebna
  znana referencna koordinata. Vzorci so casovno povezani, zlasti pri 5 Hz.
- Trenutne pospeske XYZ v g; povprecno normo |a|, njen standardni odklon in
  odmik norme od nominalnih 1 g. Posamezne osi zaradi teznosti in nagiba ne
  smejo biti samodejno nastavljene na nic. Ena orientacija ne doloci loceno
  odmikov osi in napak skaliranja; za to je potreben kalibracijski postopek.
- Trenutni ziroskop XYZ, povprecje (ocena odmika pri mirovanju) in standardni
  odklon v stopinjah/s. Izracun ne odsteva odmikov in ne kalibrira samodejno.
- Cas preizkusa, stevilo vzorcev ter napake I2C, NMEA in manjkajoce GPS epohe.

Statistika se prikazuje brez zapisa na kartico, serijski povzetek je na 2 s.
Temperaturna stabilizacija lahko traja dlje od zacetnega 10-sekundnega premora.
V nadaljevanju opisani CSV nacin velja, ko je STATIONARY_TEST=false.

## Povezave

| Modul | ESP32 |
| --- | --- |
| NEO-6 TX | GPIO19 (UART RX) |
| NEO-6 RX | GPIO4 (UART TX) |
| MPU9250 SDA | GPIO21 |
| MPU9250 SCL | GPIO22 |
| SD CS | GPIO13 |
| SD MOSI | GPIO15 |
| SD MISO | GPIO2 |
| SD SCK | GPIO14 |
| GND obeh modulov | GND |

Napajanje priklopi glede na specifikacijo svoje razvojne ploscice modula;
signali ESP32 so 3,3 V.

## Preizkus

V VS Code odpri Terminal > New Terminal in za dvosmerno komunikacijo uporabi:

```powershell
.\.venv\Scripts\platformio.exe device monitor --port COM4 --baud 115200 --echo --dtr 0 --rts 0
```

Klikni v terminal in vtipkaj posamezno crko (Enter ni potreben): s za SETUP,
z za shranitev nicle nagiba pri poravnanem mirujocem motorju, c za ponastavitev
nicle, g za preklop zelenega preizkusa in n za naslednji zaslon oziroma izhod
iz SETUP. Tudi q ali Escape vrneta na glavni zaslon. Ukaza r/p
zacneta/ustavita statisticni preizkus. Ctrl+C zapre monitor. Pred nalaganjem
programa monitor zapri, da sprosti COM4. Odpiranje serijskih vrat lahko na
nekaterih razvojnih ploscicah sprozi ponovni zagon tudi z nastavljenima DTR/RTS.

```powershell
.\.venv\Scripts\platformio.exe run
.\.venv\Scripts\platformio.exe run --target upload --upload-port COM4
.\.venv\Scripts\python.exe tools\serial_check.py --port COM4 --seconds 20
```

Serijski izpis uporablja 115200 baud. Program ob zagonu poisce GPS prek UBX
(38400 ali tovarniskih 9600 baud, nato se ostale obicajne hitrosti), povezavo
nastavi na 38400 baud in GPS nastavi na 5 Hz (200 ms) v neprekinjenem nacinu.
Uporablja UBX-CFG-PRT, CFG-MSG, CFG-RXM in CFG-RATE, preveri ACK in prebere
nastavitev CFG-RATE nazaj. Ce povezava ali nastavitev ne uspe, to jasno izpise.
GPS RX mora biti povezan na GPIO4, da modul prejme ukaze.
Nastavitve se uporabijo ob vsakem zagonu ESP32, brez shranjevanja v GPS flash.
Na UART sta omogocena GGA in RMC vsak navigacijski cikel, odvecni stavki
GLL/GSA/GSV/VTG so izklopljeni. NEO-6M/G/Q/T uradno podpirajo najvec 5 Hz;
NEO-6P/V imata po specifikaciji omejitev 1 Hz.
Program ob zagonu pregleda I2C in preveri WHO_AM_I na naslovih 0x68/0x69
(MPU6500: 0x70, MPU9250: 0x71, MPU9255: 0x73). Vsaki dve sekundi izpise pospesek v g
(vkljucno s teznostjo) in kotno hitrost v stopinjah/s. To so nekalibrirane meritve;
magnetometer v tem preizkusu ni inicializiran ali preverjen.
Po spremembi povezav MPU pritisni RESET za ponovni pregled.

Serijski izpis poteka vsakih 2000 ms (0,5 Hz). MPU se dodatno prebere ob vsakem
GPS GGA zapisu (5 Hz), ko je SD belezenje aktivno. To sta intervala
branja v programu, ne notranja frekvenca vzorcenja senzorja. GPS UART se bere
sproti v glavni zanki. Interval GPS se meri med kontrolno preverjenimi GGA
stavki z razlicnimi GPS casovnimi oznakami; prikazani so zadnji interval,
povprecje, minimum in maksimum od zagona. Loceno je prikazan korak UTC casa
meritve v GGA, ki mora biti pri 5 Hz 200 ms. To loci dejanske GPS epohe od
nihanja casa prispetja po UART. Izpis vsebuje tudi stevilo napacnih NMEA
kontrolnih vsot. 38400 baud (GPS) in 115200 baud (USB izpis) sta hitrosti
prenosa, ne frekvenci meritev. Vsaka GPS meritev se sproti razcleni tudi med
dvosekundnimi diagnosticnimi izpisi. Racunanje casa kroga se ni implementirano.

SD uporablja loceno vodilo HSPI pri 1 MHz in navedene pine. Ob zagonu se priklopi
datotecni sistem brez samodejnega formatiranja ter preverita odpiranje korenskega
imenika in branje sektorja 0. Izpiseta se kapaciteta in zasedenost.
Ob vsakem zagonu se ustvari nova datoteka /gps0000.csv, /gps0001.csv itd.;
obstojece datoteke se ne prepisujejo. Vsaka razlicna GGA casovna oznaka sprozi
en CSV zapis s stevilko vrstice, ESP32 casom sprejema, GPS UTC casom meritve
(ms od polnoci), veljavnostjo polozaja, koordinatami, stevilom satelitov ter
svezimi nekalibriranimi meritvami MPU in oznako uspesnega branja MPU.
MPU se vzorci ob sprejemu GGA; to ni casovno sinhronizirana GPS/IMU fuzija.
Po vsaki vrstici se datoteka zapre, ponovno odpre za branje in zadnja vrstica
primerja bajt za bajtom. To je stalen diagnosticni preizkus zapisovanja pri
5 Hz. Stevec prikazuje samo uspesno zapisane in prebrane vrstice, napake in
najdaljse trajanje branja MPU, zapisa in preverjanja. Ob napaki se belezenje
ustavi. To preverja branje prek datotecnega sistema; ni preizkus obstojnosti
po izpadu napajanja. GPS izpis dodatno steje manjkajoce 200-ms epohe in
napacne NMEA kontrolne vsote. Po menjavi kartice ali povezav pritisni RESET.

## SPI zaslon 480x320 (pripravljeno za drugo fazo preizkusa)

Zaslon lahko uporablja drugo vodilo VSPI, SD pa ostane na HSPI.
Predlagani pini so prosti glede na zgornje povezave:

| Signal zaslona | GPIO |
| --- | --- |
| SCK / CLK | 18 |
| MOSI / DIN / SDA | 23 |
| CS | 27 |
| DC / A0 | 26 |
| RST / RES | 25 |
| MISO / SDO (samo ce ga zaslon potrebuje) | 33 |

TFT_eSPI uporablja VSPI z eksplicitnimi pini iz include/tft_setup.h.
Privzeti VSPI MISO=19 je ze zaseden z GPS TX; zaslon uporablja MISO=33.
SD ostane na locenem HSPI vodilu. MISO zaslona je izbiren, ker prikaz ne
uporablja branja iz zaslona.
Signali morajo biti zdruzljivi s 3,3 V. VCC in osvetlitev BL/LED se povezeta
po specifikaciji konkretnega modula; podatkovni pin SDA pri SPI zaslonu pomeni
MOSI, ne I2C. Gonilnik, frekvenca in priklop dodatnega dotika so odvisni od
modela zaslona. Osvetlitev ni krmiljena prek GPIO in mora biti ustrezno napajana
glede na konkretni modul. Zaslona ne priklapljaj med delovanjem.

Oznaka 480x320 V1.0 sama ne doloca krmilnika. Kot zacetna, nepotrjena izbira
je nastavljen ILI9488 pri 20 MHz. Ce oznaka na modulu potrdi ILI9486 ali
ST7796, zamenjaj samo definicijo gonilnika v include/tft_setup.h. Drugi parametri
(barvni vrstni red, inverzija, orientacija) se po potrebi prilagodijo pri preizkusu.
Nastavitve so del projekta in se prek build_flags uporabijo tudi pri prevajanju
knjiznice; datotek znotraj prenesene knjiznice ni treba spreminjati.

Program inicializira lezec prikaz 480x320 z veliko GPS hitrostjo, stanjem
polozaja, sateliti, koordinatami, pospeski, stanjem SD, imenom CSV datoteke,
stevilom preverjenih vrstic, napakami in najdaljsim trajanjem zapisa na SD.
Cas kroga ostaja --, ker izracun se ni implementiran. Zastareli GPS podatki
se po 1,5 sekunde prikazejo kot nedosegljivi. Posamezna regija se lahko osvezi
vsakih 25 ms, celoten krog osmih regij traja najmanj 200 ms; nespremenjene
vrednosti se ne izrisujejo znova. GPS in SD se obdelujeta pred izrisom regije.
Prikaz ne caka na zaznavo zaslona, zato odklopljen zaslon ne ustavi zanke.

Trenutni zagon prikaze samo RACE za 0,8 s in nadaljuje na glavni zaslon.
Barvni preizkus in branje registrov sta bila odstranjena iz rednega zagona.

Prejsnji preizkus 2026-10-24 na COM4: program nalozen, barvni RAM preizkus 3/3 uspesen,
D3=FF FF FF FF. To potrjuje zapis in povratno SPI branje prek GPIO33 pri
izbranem gonilniku ILI9488. Osvetlitev LED/BL med preizkusom ni bila priklopljena;
vidne slike, orientacije in barv na dejanskem panelu se ni bilo mogoce potrditi.
GPS je posiljal veljavne NMEA podatke brez polozaja; SD in MPU sta se odzivala.

GPS loceno prikazuje odsotnost UART podatkov, podatke brez veljavnih NMEA stavkov
in veljavne NMEA podatke s polozajem ali brez njega. Brez satelitskega polozaja
v notranjosti se vedno lahko potrdi komunikacija z modulom. Podatki, starejsi od
petih sekund, se ne prikazujejo kot sveze meritve. Za dolocitev polozaja postavi
anteno na mesto z odprtim pogledom na nebo.

Pri nekaterih ploscicah je treba ob zacetku nalaganja drzati gumb BOOT in ga
spustiti, ko se izpise `Connecting...`.

Register map: https://invensense.tdk.com/wp-content/uploads/2017/11/RM-MPU-9250A-00-v1.6.pdf
GPS parser: https://github.com/mikalhart/TinyGPSPlus
NEO-6 specifikacija: https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf
UBX protokol: https://content.u-blox.com/sites/default/files/products/documents/u-blox6_ReceiverDescrProtSpec_%28GPS.G6-SW-10018%29_Public.pdf
