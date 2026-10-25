# ESP32 lap timer — terminal-v1

Upravljanje: **zeleni gumb** (trenutna tipka GPIO32), **rdeči gumb**
(zaskočni kontakt GPIO5) in **stikalo levo/desno** (GPIO34/GPIO35).
Zeleni je glavni gumb za prikaze, izbiro in potrjevanje. Terminal ostaja na voljo;
BLE TPMS povezava še ni preverjena.

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

Različica z ločenimi izbori in smerjo + zelenim je naložena na COM4:
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

- Skupni dogodki QUICK, SETTINGS ON/OFF in LEFT/CENTER/RIGHT. Drzano stanje
  ne ponavlja dogodkov; potrjene in validirane nastavitve so v verzioniranem NVS.
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
CALIBRATION vsebuje UPRIGHT, LEFT, FORWARD in ZERO. FORWARD omogoča izbiro osi
X+/X-/Y+/Y-/Z+/Z- pred potrditvijo. Posamezna nastavitev odpre **novo stran**:
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

## Omejitve

Dinamicni nagib je EXPERIMENTAL, brez opravljene fizicne kalibracije neveljaven.
GNSS pomoc uporablja hitrost in smer, vendar nima PPS sinhronizacije; dolgi
ovinki, naklon ceste, vibracije, zdrs in izguba GNSS se niso terensko potrjeni.
BLE dekoder še ni implementiran. Vhodi gumbov so implementirani; fizično vezavo
in delovanje novih kontaktov je še treba preveriti. Celoten seznam in
postopek kalibracije sta v terminalskem vodicu. Fuzija ne skriva surovih meritev.
Prikazov in utripanja s strani ne more potrditi serijski test sam.
