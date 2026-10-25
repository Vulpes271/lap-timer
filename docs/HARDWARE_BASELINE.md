# Historical hardware baseline

Snapshot before terminal-v1. Its old operational descriptions are superseded
by [the terminal guide](codex_terminal_test.md). The earlier switch pin proposal
is not a measured or final wiring diagram; no GPIO switch inputs are enabled.

# ESP32 lap timer

## Predlagana povezava 7-zicnega stikala (se ni programsko aktivna)

Za trenutno ESP32-WROOM-32 vezavo so prosti ti stirje vhodi:

| Kontakt | GPIO | Predvidena uporaba |
| --- | --- | --- |
| Navadni trenutni gumb | 32 | Potrdi v meniju; v rocnem nacinu oznaci krog |
| Zaskocni gumb | 16 | Izbira nacina ali omogocanje rocnega merjenja |
| Leva stran preklopnika | 17 | Prejsnja moznost |
| Desna stran preklopnika | 34 | Naslednja moznost |

To velja za WROOM brez PSRAM na 16/17. Pri menjavi modula preveri njegovo vezavo.
Vsi kontakti naj sklenejo proti GND, aktivno stanje je LOW. Trenutni in
zaskocni gumb imata vsak po en povratni vod na GND; skupni kontakt
levo/sredina/desno gre prav tako na GND. Tako je sedem zic: stirje signali
in trije GND vodi. V sredini naj levi in desni kontakt ostaneta odprta.
Razpored zic potrdi z multimetrom v nacinu prevodnosti brez napajanja;
barve in skupni kontakt se iz opisa ne dajo dolociti.

GPIO34 OBVEZNO potrebuje zunanji 10 kohm upor proti 3,3 V (nima notranjega
pull-up). Za 32/16/17 se lahko uporabi INPUT_PULLUP; pri daljsih zicah so
smiselni zunanji 10 kohm upori na vseh stirih vhodih. V tej vezavi VCC ni
skupni kontakt stikala: 3,3 V gre samo prek uporov na signale. GPIO ne
priklapljaj na 5 V ali na motorne 12 V/obstojeco vezavo smernikov.
GPIO35 ostane prost npr. za poznejso analogno meritev baterije prek delilnika.

Program se ne bere teh kontaktov. Preslikava menijev, razodboj kontaktov in
rocno merjenje bodo dodani ob priklopu; trenutni GPS timing ostaja aktiven.
Vir za omejitve GPIO34 in pull-up:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html

## Izris brez vmesnega brisanja besedila

Dinamicno besedilo se sestavi v 1-bitnem pomnilniskem platnu 480x144.
Maska prejsnjega izrisa (19200 B) omogoca posiljanje samo spremenjenih
odsekov vrstic. Na zaslon pridejo koncne barve; ni locenega brisanja in
ponovnega pisanja vseh stevilk. Veliki cas zato ob spremembi desetinke ne
osvezi preostalih nespremenjenih stevilk. Znak stopinj je del istega izrisa.
Stranski okvirji se spreminjajo posamicno, noga mirujocega testa pa le ob
vstopu na zaslon. Celoten zaslon se brise samo ob zamenjavi strani ali zagonu.
Ob neuspesni dodelitvi 8640 B za platno se izpise opozorilo in uporabi stari
neposredni nacin. To zmanjsuje programsko utripanje; ne sinhronizira pisanja
s panelnim osvezevanjem in ne odpravlja utripanja osvetlitve/napajanja.
Ukaz v izpise stanje platna, prosti RAM, trajanje in stevilo poslanih pik.
Na glavnem zaslonu prebere se vzorec besedila in ozadja iz TFT RAM za
preverjanje prenosa in vrstnega reda barv. Na COM4 sta oba vzorca PASS
(FFFF in 0841), preklopi zaslonov delujejo, timing testi so 20/20.
Vizualno utripanje s strani se potrdi na fizicnem zaslonu.

## Timing: Pomposa Long

Proga je nalozena iz javnega uradnega AiM izvoza evropskih prog. Izvoz ima
datum 2019-10-28 in zapis PomposaMotoL (poleg razlicnih Pomposa in Pomposa Moto).
V projektu je poimenovan Pomposa Long. To je zgodovinski zapis dolge moto
konfiguracije; enakost z danasnjo Solo 2 oznako Pomposa Long NI potrjena.
Za neposredno primerjavo s Solo 2 preveri njegov aktualni izvoz start/cilja.

- AiM start/cilj: 44.710284 N, 12.214867 E.
- Smer voznje, izpeljana iz sosednjih tock AiM trase: priblizno 347,02 stopinje.
- Virtualna crta je pravokotna na smer, dolga 40 m. Sirina je nasa nastavitev,
  ne vrednost iz AiM izvoza; vpliva na dopustno stransko oddaljenost prehoda.
- Izvor, SHA256 izvoza, datum, ime in tocke za izpeljavo smeri so zapisani v
  tracks/pomposa_long.json. Celotne tuje baze in binarnih tras ne objavljamo.

Ob zagonu ostaneta cas 0:00.0 in LAP 0 do prvega veljavnega usmerjenega prehoda.
Prvi prehod zacne LAP 1. Vsak naslednji veljavni prehod zakljuci krog, shrani
zadnji in najboljsi cas ter zacne naslednjega. Zadnji/best sta v SETUP in na USB.
Tocke se pretvorijo v lokalne metre in preveri se presecisce s koncnim odsekom
crte. Cas prehoda je linearno interpoliran iz predznacenih pravokotnih razdalj
in GPS UTC casov meritev; cas prejema po UART se ne uporablja za rezultat.
Prikaz med vzorcema le tece z lokalno uro. Prehod ob polnoci je podprt.

Trenutne nastavitve zascit:

- Veljaven fix, najmanj 4 sateliti, HDOP >0 in <=3, svezi podatki o hitrosti.
- Pred prehodom se je treba priblizati vsaj 15 m pred crto; prehodi v nasprotni
  smeri in izven 40-metrskega odseka se ne stejejo.
- Hitrost obeh vzorcev najmanj 10 km/h; tudi interpoliran premik naprej mora
  ustrezati najmanj 10 km/h.
- Najkrajsi krog 30 s, najdaljsi 10 min. Jitter ob cilju ne ustvari novega kroga.
- Ne povezujemo GPS vzorcev z razmikom nad 1 s. Slab fix, izpad nad 1,5 s,
  skok polozaja ali predolg krog prekinejo trenutni krog. Zadnji in najboljsi
  veljavni cas ostaneta; za nadaljevanje je potreben nov veljaven prehod.
- Merjenje se ne sprozi vec kot 1500 m od cilja. Pri premiku med progami se
  zgodovina rezultatov ne menja samodejno; za novo sejo ponovno zazeni napravo.

### Trije sektorji in izbira dirkalisca

AiM zapis PomposaMotoL vsebuje samo eno line_info (start/cilj), brez vmesnih
tock. Uvoznik iz prenesene urejene trase izpelje LASTNI meji pri 1/3 in 2/3
njene dolzine. To niso uradni AiM sektorji. Koordinate in metoda so v JSON:

- S1 konec: 44.712024124 N, 12.214997752 E.
- S2 konec: 44.708885544 N, 12.214549052 E.
- S3 konec: start/cilj.

Vsaka meja ima svojo smer, 40 m sirine in 15 m priblizevanja. Casi uporabljajo
enako GPS UTC interpolacijo kot cilj. Krog se shrani samo po zaporedju
start -> S1 -> S2 -> cilj. Ce manjka sektor, se ob cilju zacne nov krog brez
shranitve nepopolnega. Zadnji trije sektorski casi so v SETUP.
Po prvem celotnem krogu se na vsakem sektorju primerja kumulativni cas z istim
mestom najboljsega celotnega kroga. Zelena pomeni hitreje, rdeca pocasneje;
primerjava ostane do naslednjega sektorja, na cilju se ponastavi. To je
primerjava na mejah sektorjev, ne zvezna napoved casa. Ukaz g vklopi TEST.

S odpre SETUP, K odpre DIRKALISCA, 1 izbere Pomposa Long. Seznam je v
include/track_catalog.h in je pripravljen za dodajanje prog. Ponovna izbira
iste proge ohrani sejo; menjava proge ponastavi cas in reference. Trenutno je
samo ena proga, ki je tudi privzeta ob zagonu. S v podmeniju vrne SETUP,
Q ali ESC pa glavni zaslon. Izbira se izvaja prek USB, fizicni gumb sledi.

Pospesek IMU se se ne uporablja za interpolacijo. Zvezna napovedna primerjava
po razdalji in samodejno prepoznavanje prog se nista implementirana. Tri meje
ne preverjajo celotne prevozene trase ali vseh moznih bliznjic.
SD belezenje ostaja izklopljeno po prejsnji nastavitvi; rezultati so v RAM-u.

### Ponovljiv uvoz in test

```powershell
.\.venv\Scripts\python.exe tools/import_aim_pomposa.py
# Ali brez ponovnega prenosa:
.\.venv\Scripts\python.exe tools/import_aim_pomposa.py --archive pot\europe.zwd
```

Uvoznik zahteva tocno PomposaMotoL, preveri obliko in lokacijo koordinat ter
generira include/pomposa_track.h in tracks/pomposa_long.json. Napaka prenosa ali
formata ne ustvari izmisljene konfiguracije. Vir:
https://aim-sportline.com/aim-software-betas/Software/Applications/GPSManager/release/export_rs3_tracks_Europe.zwd
AiM opis izvoza:
https://aim-sportline.com/download/software/doc/Import-RS3-tracks-into-GPSManager_100_eng.pdf

Serijski ukaz t izvede izolirane sinteticne C++ preizkuse timinga na ESP32;
ne spremeni aktivne seje, kalibracije ali SD. Preveri prehod/interpolacijo,
smer, meje crte, mirovanje, jitter, kakovost in izpad GPS, polnoc ter dva kroga.
Razsirjeni preizkus preveri tudi tri sektorske case, hitrejso/pocasnejso
primerjavo, izpuscen sektor in menjavo proge. Na ESP32 COM4 je uspelo 20/20
sinteticnih preverjanj; podmeni izbire in vrnitev sta preverjena prek USB.
To ni nadomestilo za terenski preizkus s fizicno znano ciljno crto.

Diagnostika GPS NEO-6, MPU6500/9250/9255 in SD kartice na ESP32-WROOM-32.
Wi-Fi test je odstranjen.

## Prva verzija: povezava GPS, IMU in zaslona

Prva integrirana verzija podpira ESP32, NEO-6 pri 5 Hz, MPU6500/9250/9255,
SPI TFT 480x320 in preverjen SD CSV zapis. Timing je opisan zgoraj; tlaka in
baterija ostajajo brez senzorjev, zapis SD izklopljen.
ZELENI PREIZKUS se vklopi z g: vsi trije vecji okvirji ob obeh
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
Zeleni preizkus je zacasen in je ob vsakem zagonu izklopljen.
Senzorska fuzija in fizicni gumb se nista implementirana. V meniju so trenutni
nagib, shranjena nicla in stanje preizkusa; isti ukazi bodo lahko povezani z gumbom.

## Trenutni zacetni zaslon: demonstracija casa kroga

Ob zagonu se za 0,8 s prikaze napis RACE, nato se odpre prvi zaslon:

- Na sredini je velik cas M:SS.d; do starta je 0:00.0.
  Pod 10 minutami se uporablja trikratna velikost pisave 7 (456 x 144 pik
  za prikaz M:SS.d); ob morebitnem daljsem krogu se pisava zmanjsa, cas se ne ovije.
  Cas se zacne ob prehodu cez nastavljeno crto, ne od vklopa naprave.
- Nad casom je manjsi LAP 0 (stevilka trenutnega kroga; pred voznjo 0).
  Levo in desno ob casu so trije vecji okvirji primerjalnega indikatorja.
  Negativna delta proti najboljsemu krogu na isti sektorski meji obarva indikator
  zeleno, pozitivna rdece, nicelna je nevtralna. Vsak segment predstavlja
  zacetih 0,2 s razlike, najvec trije segmenti. Zraven LAP je izpis BEST z delto
  v sekundah. Ko je zeleni preizkus izklopljen in ni reference, je BEST -- in indikator siv.
  Primerjava se posodobi na mejah sektorjev; zvezna napovedna primerjava po
  razdalji se ni implementirana. Lucke so na zaslonu,
  ne fizicne LED diode.
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
  Stevilka je absolutna vrednost z znakom stopinj (krogec), brez deg in L/D; smer pokaze pravokotnik.
  Stevilka prikaze tudi vrednosti zunaj tega obmocja, kazalnik pa se omeji
  na konec merila. Brez svezega MPU podatka se izpise -- z znakom stopinj.

Trenutni nagib je staticna ocena atan2(ay, az), zmanjsana za shranjeno niclo
in ovita na -180 do +180 stopinj. Brez nastavitve je nicla ay=0 in az>0;
predznak in os sta odvisna od namestitve MPU.
To ni kalibriran kot nagiba motorja med voznjo: linearni pospeski in zavoji
vplivajo na pospeskomer. Nastavitev nicle namestitve je v SETUP; senzorska
fuzija sledi kasneje. Demonstracijski cas in tlaki so v DashboardData loceni od izrisa.

DashboardScreen ima zaslone LapTimer, Diagnostics, Stationary, Settings in Tracks. Privzeti je
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
dvosekundnimi diagnosticnimi izpisi. Merjenje krogov za Pomposa Long je opisano
na zacetku dokumenta.

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
Glavni zaslon prikazuje interpolirani cas kroga za nastavljeno progo; diagnostika
prikazuje zgoraj nastete meritve. Zastareli GPS podatki
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
