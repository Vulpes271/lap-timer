# ESP32 lap timer — terminal-v1

Upravljanje: **zeleni gumb** (trenutna tipka GPIO32), **rdeči gumb**
(zaskočni kontakt GPIO5) in **stikalo levo/desno** (GPIO34/GPIO35).
Zeleni je glavni gumb za prikaze, izbiro in potrjevanje. Terminal ostaja na voljo.
BLE sprejem TPMS še ni implementiran; pripravljena sta podatkovni vmesnik in simulator.

## Pregled trenutne implementacije

| Področje | Kaj je implementirano |
| --- | --- |
| GPS | NEO-6 prek UART, nastavitev 5 Hz, položaj, hitrost, smer, UTC, sateliti in zaznavanje izgube signala. |
| Merjenje krogov | Prehod startno-ciljne črte v določeni smeri, časovna interpolacija med meritvama GPS, štetje krogov, trije sektorji in primerjava z najboljšim krogom. |
| Dirkališča | Katalog in izbira proge v nastavitvah; trenutno Pomposa Long. Izvor trase in projektni sektorski meji sta opisana spodaj. |
| Zasloni | RACE in CAMERA s časom, tlaki, statusom GPS/SD, nagibnim barom in tremi primerjalnimi okvirji na vsaki strani; CAMERA doda hitrost. Ločeni veliki prikazi zadnjega/najboljšega kroga, tlakov in maksimumov. |
| Upravljanje | Zeleni in rdeči gumb ter stikalo levo/desno, zaščiten vstop v nastavitve, večstopenjski meniji, ločeni izbori, ponavljanje številčnih vrednosti ob držanju in nastavljivi trije hitri prikazi. |
| Nastavitve | DISPLAY, CALIBRATION, UNITS, TRACK, TPMS in STORAGE; tri ozadja, enote, meje tlakov, čas prikaza kroga in nastavitve beleženja. Potrjene nastavitve se ohranijo v NVS. |
| IMU | Neodvisen zajem pri nominalno 50 Hz, surovi nagib pred kalibracijo, vodena kalibracija pokonci/levo/desno za poljubno zasukano montažo, odmik giroskopa in ločena ničla nagiba. Dinamična ocena ostaja eksperimentalna. |
| SD | CSV za REAL in TEST v ločenih imenikih, zapisovalna naloga z vrsto in števci napak, shranjevanje dnevne reference BEST, seznam datotek z velikostmi ter formatiranje FAT32 z izrecno potrditvijo. |
| Tlaki | Vmesnik za oba senzorja, izolirani testni podatki, alarmi LOW/HIGH, histereza, potrditev in zaznavanje zastarelih meritev. Dejanski senzorji BLE še niso povezani. |
| Terminal in diagnostika | Ukazi za prikaze, nastavitve, simulacijo gumbov/senzorjev in diagnostiko GPS/IMU/SD; ločeni testi timinga, krmiljenja ter novih SD/IMU funkcij. |

**Stanje preverjanja:** zadnje spremembe SD/IMU so prevedene za ESP32 in imajo
35 uspešnih ciljnih preverjanj na računalniku. Na ploščico še niso naložene,
ker pri zadnjem preverjanju USB ni bilo zaznanega COM priključka. Spodaj navedeni
strojni rezultati veljajo za predhodno naloženo različico. Dejanskega formatiranja
kartice in nove kalibracije na motorju še nismo izvedli.

Interpolacija trenutno uporablja meritve GPS; vključitev pospeška IMU v izračun
trenutka prečkanja črte še ni implementirana. Prav tako niso implementirani
sprejem senzorjev BLE TPMS in merjenje baterije; zanju je na zaslonu predviden prostor.

Navodila, vsi ukazi in rocni preizkusi:
**[docs/codex_terminal_test.md](docs/codex_terminal_test.md)**.
Izvirni nacrt iz pulla: [CODEX_TERMINAL_TEST_PLAN.md](docs/CODEX_TERMINAL_TEST_PLAN.md).
Zgodovinski opisi strojnih testov: [HARDWARE_BASELINE.md](docs/HARDWARE_BASELINE.md).

## Zagon in preverjanje

V PowerShell terminalu projekta:

```powershell
.\.venv\Scripts\platformio.exe run
.\.venv\Scripts\platformio.exe run --target upload --upload-port COM4
.\.venv\Scripts\platformio.exe device monitor --port COM4 --baud 115200 --filter send_on_enter --echo
```

Vsak ukaz potrdi z Enter. Ctrl+C zapre monitor. Monitor mora biti zaprt za
nalaganje ali avtomatski test. Iz osnovnega terminala:

```text
help
status
selftest
screen camera
screen race
switch settings on
key right
key quick
key right
key quick
switch settings off
```

Samodejni integracijski preizkus na ploscici (obnovi uporabnikove nastavitve):

```powershell
.\.venv\Scripts\python.exe tools/test_terminal.py --port COM4
```

Predhodna različica z ločenimi izbori in smerjo + zelenim je bila naložena na COM4:
**20/20 testov timinga in 139/139 testov krmiljenja** je uspešnih. Na ploščici je
izmerjen dvig tlaka za **2,00 bar v 4,08 s**, brez shranjevanja preizkusne spremembe.
Terminalski preizkus je preveril 19 scenarijev do vključno izbora barv in ponavljanja
tlaka (+2,20 bar v 4,62 s). Nato je GPS zaščita blokirala ponovni vstop. Preostala
tri preverjanja BACK/ponovnega odklepanja so uspešno izvedena ločeno. Zaščita med
testi ni bila izklopljena; celoten neprekinjen terminalski tek zato ni označen kot PASS.
CAMERA na nekalibrirani ploščici vrača veljaven RAW nagib.
Nalaganje in zadnji testi so bili izvedeni brez SD kartice. Kontakti se pri terminalskem testu
simulirajo; fizične pritiske in ožičenje mora potrditi še ročni preizkus.
Starejši terminalski preizkus je preveril 28 scenarijev ter SD readback/reference.
Med menjavo zaslonov je preverjeni del CSV vseboval IMU intervale 19,454..20,499 ms.

## Izvedeno

Hitri prikazi imajo samo naslov in veliko vrednost: LAP s stevilko kroga in
casom oziroma REAR/FRONT PRESSURE z vrednostjo in enoto. V SETUP so GREEN 1/2/3
z osmimi izbirami (zadnji/najboljsi krog, oba tlaka, najvecji nagib, hitrost,
pospesek in pojemek) ter BACKGROUND: crno, belo ali temno modro. Enote ostanejo
nastavljive prek SPEED/PRESSURE/TEMPERATURE/ACCEL UNIT. Nastavitve se shranijo
v NVS v4, stari zapis v3 se samodejno prenese. Maksimumi so za trenutno sejo;
brez veljavnih meritev je `--`. Podrobnosti in ukazi so v terminalskem vodicu.

- Skupni dogodki QUICK, SETTINGS ON/OFF in LEFT/CENTER/RIGHT, odpravljanje odboja
  kontaktov in nadzorovano ponavljanje številčnih vrednosti; potrjene in validirane
  nastavitve so v verzioniranem NVS.
- RACE, CAMERA, LAST LAP, FRONT/REAR PRESSURE, petsekundni hitri pregledi,
  LAP HOLD 5..15 s in zelen BEST. Menjava zaslona ne ustavi merjenja ali SD.
- Trije sektorji Pomposa Long z GPS UTC interpolacijo. Dnevni BEST je celoten
  krog in njegovi sektorji, izoliran po datumu in geometriji; SD hrani dve
  preverjeni rezi z moznostjo obnovitve. Brez veljavnega GNSS datuma je SESSION.
- Vmesnik TPMS in izoliran simulator. LOW/HIGH alarm prikaze dejanski tlak,
  zahteva potrditev svezih meritev in uposteva histerezo ter izgubo senzorja.
  Privzeto OFF; meje in identitete se nastavijo izrecno. BLE radia ne simuliramo
  kot fizicno povezanega senzorja.
- Neodvisen zajem IMU pri nominalno 50 Hz z dejanskim dt, voden zajem bias/rotacije,
  locena nicla nagiba in oznacena eksperimentalna dinamicna ocena.
- SD zapisovalnik v svoji nalogi: vrsta, odprta datoteka, redno praznjenje,
  casovne oznake tokov in stevci izgubljenih zapisov/napak. `/sessions/` za REAL,
  `/test/` za TEST. Zapis je privzeto OFF, `set logging on` ga vklopi in shrani.
- Izris v pomnilniku in prenos spremenjenih delov besedila. Trije primerjalni
  okvirji ob obeh straneh, stopinje z znakom, inkrementalen trak nagiba.

## Gumbi, vezava in navigacija

| Upravljalnik | GPIO | Vezava |
| --- | --- | --- |
| Zeleni gumb, trenutni kontakt | 32 | Kontakt med GPIO32 in GND, INPUT_PULLUP |
| Rdeči gumb, zaskočni kontakt | 5 | Kontakt med GPIO5 in GND, INPUT_PULLUP |
| Stikalo levo | 34 | Levi kontakt na GPIO34, obvezen 10 kΩ proti 3,3 V |
| Stikalo desno | 35 | Desni kontakt na GPIO35, obvezen 10 kΩ proti 3,3 V |
| Skupni kontakt stikala levo/desno | GND | V srednjem položaju sta oba kontakta odprta |

GPIO34 in GPIO35 sta samo vhoda in nimata notranjih pull-up uporov. **Vsak potrebuje
svoj upor 10 kΩ proti 3,3 V (3V3), ne proti 5 V/VIN.** Upora sta lahko pri ploščici,
ob pinih 34/35. Pri polmetrski žici je zunanji 10 kΩ proti 3,3 V priporočljiv tudi
za GPIO32 in GPIO5. Pritisnjen kontakt pomeni LOW. GND je skupen vsem kontaktom.
Skupni priključek stikala preveri z merilnikom; ni nujno fizično sredinski pin.

GPIO2/13/14/15 ostanejo za SD, čeprav se ob odklopljeni kartici zdijo prosti.
GPIO16/17 nista uporabljena. GPIO0 pustimo zaradi zagonskega načina in GPIO12
zaradi izbire napetosti flasha. GPIO5 je zagonski pin za časovni rob SDIO slave;
te funkcije projekt ne uporablja (SD uporablja SPI). Po resetu deluje kot vhod.
[Espressif: GPIO in zagonski pini](https://documentation.espressif.com/esp32_datasheet_en.html).

| Dejanje | Rezultat |
| --- | --- |
| Kratek pritisk zelenega, nato spust | Naslednji od treh izbranih prikazov; po 5 s vrnitev |
| Rdeči OFF → ON | Samo omogoči poskus vstopa; meni se še ne odpre |
| Stikalo v sredino, nato drži zeleni 2 s | Vstop v nastavitve, če ni aktivnega kroga, hitrosti nad 5 km/h ali nepotrjenega alarma |
| Stikalo levo + pritisk zelenega | En korak gor oziroma zmanjšanje vrednosti |
| Stikalo desno + pritisk zelenega | En korak dol oziroma povečanje vrednosti |
| Stikalo v sredino + kratek zeleni | Odpre razdelek ali stran z izbirami; potrdi izbrano vrednost in vrne nazaj |
| Stikalo v sredino + dolg zeleni v meniju | Enaka izbira/potrditev; spust ne doda klika |
| Drži stikalo levo/desno in drži zeleni na številčni vrednosti | Prvi korak takoj, po 600 ms se koraki ponavljajo |
| BACK v podmeniju | Vrne na seznam razdelkov |
| BACK na seznamu razdelkov | Zapre meni in vrne na RACE |
| Rdeči OFF | Takojšen izhod; nepotrjena sprememba se zavrže |

Stikala med koraki ni treba vračati v sredino: pusti ga v izbrani smeri in klikaj
zeleni. Sam premik stikala ne premakne menija. Na seznamih in pri barvah/enotah
je en pritisk en korak, tudi ob dolgem držanju. Pri tlaku se po začetnem zamiku
ponavlja na **80 ms (12,5 koraka/s)** s korakom **0,05 bar**: od 0 do 2 bar približno
3,64 s držanja, brez upoštevanja zakasnitev izrisa. Druge številke imajo 250 ms
med koraki (4/s). Spust zelenega ali sprememba smeri med držanjem ustavi
ponavljanje; za nadaljevanje spet pritisni zeleni. Premik v sredino med držanjem
ne sme postati potrditev. Obe sklenjeni smeri ne izbereta ničesar.
Zunaj menija zeleni še vedno odpira hitre prikaze, alarm pa potrdi kratek zeleni.
Razpored ponavljanja ohranja ritem ob manjših zamudah izrisa; po daljši prekinitvi
ne naredi serije zamujenih korakov naenkrat. Dejanska meritev na ploščici je 4,08 s
za +2,00 bar, kar izpolni cilj največ petih sekund.

Po izhodu z izbiro BACK rdeči lahko ostane ON, vendar moraš za nov
vstop narediti OFF → ON in nov dvosekundni pritisk zelenega. Rdeči ON ob zagonu
ne odklene menija, zeleni ob zagonu pa zahteva spust pred uporabo. Če že držiš
zeleni in šele nato vklopiš rdeči, to ne odpre nastavitev.

Zaščita uporablja **dejanski** merilnik kroga in dejansko GPS hitrost, tudi v TEST.
Veljavna hitrost nad 5 km/h je sveža manj kot 1,5 s. Premik med držanjem prekine
poskus vstopa. Začetek kroga ali zaznano gibanje nad pragom zapre tudi že odprt
meni in zahteva ponovno odklepanje. Brez sveže GPS hitrosti ostane dvostopenjski
vstop; to ni dokaz mirovanja in ni varnostno certificirana blokada.

Terminalska simulacija uporablja isto zaščiteno logiko:

```text
button red off
button direction center
button red on
button green down
```

Počakaj 2 sekundi, nato `button green up`. Za en korak:

```text
button direction right
button green down
button green up
button green down
button green up
button direction center
```

To premakne izbiro dvakrat desno. Zdaj je stikalo v sredini: naslednji kratek
zeleni odpre izbrani razdelek/nastavitev ali potrdi vrednost.
Za izhod izberi BACK s kratkim pritiskom ali uporabi rdeči OFF.
Končaj z `button red off`. Ukaza `key green` in stari `key quick` pomenita
neposreden kratek klik. Stari `switch settings on/off` in `key left/right` so
neposredni diagnostični ukazi za terminal in obidejo fizično odklepanje; za
preverjanje zaščite uporabi `button ...`. Fizičnega ožičenja simulacija ne potrdi.

Meni uporablja velike tiskane črke in seznam s petimi vidnimi vrsticami.
Izbrana vrstica je rdeča, potrjena nastavitev pa zelena do naslednjega premika.
Prvi razdelki so **DISPLAY, CALIBRATION, UNITS**, sledijo TRACK, TPMS, STORAGE in
BACK. V DISPLAY so osnovni zaslon, GREEN 1/2/3, ozadje in prikaz kroga/delte;
CALIBRATION vsebuje IMU CALIBRATE za voden postopek pokonci/levo/desno ter IMU ZERO
za ločeno nastavitev ničle. Ročna izbira osi ostaja dosegljiva prek starih
terminalskih ukazov za diagnostiko. Posamezna nastavitev odpre **novo stran**:
na primer BACKGROUND pokaže BLACK, WHITE, DARK BLUE in BACK z velikimi črkami.
Izbrana možnost je rdeča, shranjena zelena. Zeleni v sredini potrdi in vrne na
prejšnji seznam; BACK v izboru zavrže osnutek. Številke imajo svojo stran z veliko
vrednostjo. Pri tlaku prvi sredinski pritisk potrdi MIN in preklopi na MAX,
naslednji shrani celoten par in vrne na seznam. Rdeči OFF zavrže nepotrjene spremembe.

CAMERA uporablja obliko glavnega zaslona: R zgoraj levo, F zgoraj desno,
čas kroga na veliko, hitrost pod časom na sredini in spodnji bar nagiba s stopinjami.
Čas in hitrost sta ločena, da ostaneta oba podatka vidna. Brez opravljene kalibracije
bar prikazuje nagib iz pospeškometra z majhno oznako **RAW** v zgornji statusni vrstici,
ob upoštevanju obstoječe ničle nagiba. To je približek iz
smeri gravitacije, občutljiv na pospeševanje; ne potrdi dinamične ocene, maksimumov
ali kalibracije. Ob izgubi IMU ali zastarelih podatkih ostane `--`.

Samodejni preizkus navigacije (ploščica miruje, REAL, meni zaprt; med testom ne
premikaj kontaktov):

```powershell
.\.venv\Scripts\python.exe tools/test_panel.py --port COM4
```

Test preveri vstop, kratki/dolgi pritisk, smeri, zavrženje osnutka, izhod in
ponovno odklepanje. Potrjene nastavitve ostanejo enake. Dnevnik je v
`.platformio/panel_integration.log`; `selftest` vsebuje tudi blokade gibanja,
aktivnega kroga, zagona z zadržanimi kontakti in prehoda števca časa.

## Kratek TEST

```text
test on
test lap 90000
test lap 88000
test tick 6000
test sector 1 28000
test sector 2 57000
status
test off
```

TEST je stalno oznacen in ne spremeni dejanske reference. Celoten primer alarma,
ponovne oborozitve, enot in SD je v terminalskem vodicu. Tamkajsnje vrednosti
2.0/3.0 bar so **samo simulacijski primer**, ne priporocilo za pnevmatike.

## Strojna oprema

| Povezava | ESP32 GPIO |
| --- | --- |
| GPS TX -> ESP32 RX | 19 |
| GPS RX <- ESP32 TX | 4 |
| IMU SDA / SCL | 21 / 22 |
| SD CS / MOSI / MISO / SCK | 13 / 15 / 2 / 14 |
| TFT CS / DC / RST | 27 / 26 / 25 |
| TFT MOSI / MISO / SCK | 23 / 33 / 18 |

NEO-6: 5 Hz, UART 38400. USB: 115200. WHO_AM_I 0x70 pomeni MPU6500.
TFT: konfiguriran ILI9488, lezece 480x320; osvetlitev je trenutno zunanje napajana.

Pri zadnjih poskusih je nalaganje uspelo po odstranitvi SD kartice. SD MISO je
na GPIO2, ki mora biti pri vstopu v nalagalnik nizek ali nepovezan; SD vezava
lahko vpliva nanj. Ob podobni težavi odstrani kartico pri odklopljenem USB in
poskusi nalaganje. To je preizkušen obvoz, ne dokončna potrditev vzroka vseh
prejšnjih napak povezave. Za običajno uporabo vrni kartico pri odklopljenem USB.
[Espressif: GPIO2 in nalagalni način](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

AiM izvor start/cilja: zgodovinski PomposaMotoL iz 2019. Sektorski meji sta
projektni tretjini prenesene trase, ne uradni AiM tocki. Ujemanje s trenutno
Solo 2 konfiguracijo ni potrjeno. [Izvor in geometrija](tracks/pomposa_long.json).
Ponovljiv uvoz: `python tools/import_aim_pomposa.py`; aktualna geometrija ostane
v generiranem `include/pomposa_track.h`, katalog pa v `include/track_catalog.h`.

## SD datoteke in vodena kalibracija IMU

V `SETUP > STORAGE > SD FILES` so imena datotek in velikosti. Seznam ima
po pet datotek ter `NEXT PAGE`, `PREVIOUS PAGE` in `BACK`. Pregleda korenski
imenik in do tri nivoje podimenikov. Pregled poteka postopoma med zapisi CSV.
Z levo/desno in zelenim gumbom izbereš vrstico, na sredini jo potrdiš.

`SETUP > STORAGE > FORMAT SD` najprej ponudi `NO - KEEP FILES`.
Samo izrecna izbira `ERASE ALL FILES` formatira kartico v FAT32 in izbriše
vse datoteke, tudi shranjene reference najboljših krogov. Pred tem izklopi
`LOGGING`; motor mora mirovati, brez aktivnega kroga ali alarma, v načinu REAL.
Med formatiranjem ne odklapljaj napajanja. Običajen zagon kartice ne formatira.

`SETUP > CALIBRATION > IMU CALIBRATE` vodi skozi tri položaje:

1. Motor postavi pokonci v želeno ničelno lego in na sredini pritisni zeleni gumb.
2. Po meritvi in napisu `OK` motor nagni **levo za vsaj 5°** in znova potrdi.
3. Po naslednjem `OK` ga nagni **desno za vsaj 5°** in potrdi.

Pri vsakem položaju meri dve sekundi. Dopušča manjši šum; preverja spremembo
smeri gravitacije, raztros pospeška in giroskopa ter vrtenje glede na izmerjeni
odmik giroskopa. Ob premikanju meritev ponavlja. Nagiba sta lahko različno velika,
do 70°, vendar morata biti na nasprotnih straneh iste ravnine nagibanja.
Iz treh položajev določi osi motorja tudi pri zasukanem senzorju. Šele uspešno
zaključen celoten postopek shrani kalibracijo in novo ničlo. Rdeči gumb OFF
prekine postopek in ohrani dosedanjo kalibracijo. Postopek izvajaj brez delovanja
motorja; vibracije delujočega motorja niso namen tega preverjanja mirovanja.

Terminalski ukazi (vsakemu sledi Enter):

```text
sd files
sd files 5
sd format
imu calibrate
imu next
imu status
imu cancel
selftest storage-imu
```

`sd format` samo izpiše navodilo za izrecno potrditev; sam ne briše.
`imu next` začne meritev trenutnega položaja; naslednjega začneš šele, ko motor
postaviš po novem navodilu. `selftest storage-imu` preverja samo novo logiko
SD/IMU s sintetičnimi meritvami. Ne briše kartice in ne shrani kalibracije.
Dejansko formatiranje in fizična kalibracija ostajata ločena ročna preizkusa.

Preverjanje te spremembe: prevod ESP32 uspešen (flash 719054 B / 54,9 %,
statični RAM 52840 B / 16,1 %); novih 35 SD/IMU preverjanj C++ na računalniku
uspešnih, brez brisanja kartice ali zapisov NVS. Starih testnih sklopov nismo
ponavljali. Nalaganje in fizično preverjanje še čakata na USB povezavo ploščice.

## Omejitve

Dinamicni nagib je EXPERIMENTAL, brez opravljene fizicne kalibracije neveljaven.
GNSS pomoc uporablja hitrost in smer, vendar nima PPS sinhronizacije; dolgi
ovinki, naklon ceste, vibracije, zdrs in izguba GNSS se niso terensko potrjeni.
BLE dekoder še ni implementiran. Vhodi gumbov so implementirani; fizično vezavo
in delovanje novih kontaktov je še treba preveriti. Celoten seznam in
postopek kalibracije sta v terminalskem vodicu. Fuzija ne skriva surovih meritev.
Prikazov in utripanja s strani ne more potrditi serijski test sam.
