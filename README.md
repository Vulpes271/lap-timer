# ESP32 lap timer

Merilnik krogov z GPS NEO-6, IMU MPU6500/9250, zaslonom SPI 480 × 320,
kartico SD in fizičnimi gumbi. Običajna različica vsebuje vse funkcije merjenja,
prikaza, nastavitev in beleženja. Simulator in samodejni testi so v ločeni
razvojni različici, ki je privzeto izklopljena.

## Implementirane funkcije

| Področje | Trenutno stanje |
| --- | --- |
| GPS | UART, nastavitev 5 Hz, položaj, hitrost, smer, UTC, sateliti in diagnostika izgube signala. |
| Timing | Prehod črte v določeni smeri, interpolacija časa med meritvama GPS, štetje krogov in trije sektorji. Trenutna proga je Pomposa Long. |
| BEST | Referenca celotnega najboljšega kroga in njegovih sektorjev za isti lokalni dan ter geometrijo proge. Na SD sta dve preverjeni reži za obnovitev. |
| RACE/CAMERA | Velik čas, tlaka R levo/F desno, GPS/BAT/SD status, številka kroga, spodnji bar nagiba ter trije primerjalni okvirji na vsaki strani. CAMERA doda hitrost in pred kalibracijo pokaže surovi nagib z oznako RAW. |
| QUICK INFO | Tri nastavljive hitre strani: zadnji/najboljši krog, oba tlaka, največji nagib, hitrost, pospešek ali pojemek. Povratek po petih sekundah. |
| SETTINGS | Hierarhični meniji z BACK na prvi, ob vstopu izbrani vrstici; ločeni izbori, tri ozadja, enote, kalibracija, proga, tlaki in SD. Potrjene nastavitve se ohranijo v NVS v4. |
| IMU | Neodvisen zajem pri nominalno 50 Hz z dejanskim časovnim korakom, vodena kalibracija pokonci/levo/desno, odmik giroskopa in ločena ničla nagiba. Dinamična ocena ostaja eksperimentalna. |
| SD | CSV v ločenih imenikih REAL/TEST, zapisovalna naloga z vrsto, redno praznjenje, števci napak, seznam datotek z velikostmi in formatiranje FAT32 z izrecno potrditvijo. |
| Tlaki | Podatkovni vmesnik obeh senzorjev, alarmi LOW/HIGH, histereza, potrjevanje svežih meritev in zaznavanje izgube senzorja. Sprejem BLE še ni implementiran. |
| Terminal | Status, nastavitve in diagnostika GPS/IMU/SD/zaslona; v razvojnem buildu še simulator in testi. |

Osnovni prikazi in zajem se nadaljujejo tudi ob menjavi zaslona. Izris besedila
poteka prek medpomnilnika; prenesejo se spremenjeni deli, bar nagiba se dopolnjuje
oziroma krajša. Vtis utripanja zahteva tudi vizualni preizkus zaslona.

## Gumbi in povezave

| Povezava | ESP32 GPIO / vezava |
| --- | --- |
| Zeleni gumb, trenutni kontakt | GPIO32 proti GND, INPUT_PULLUP |
| Rdeči gumb, zaskočni kontakt | GPIO5 proti GND, INPUT_PULLUP |
| Stikalo levo | GPIO34, lasten 10 kΩ proti 3,3 V, kontakt proti GND |
| Stikalo desno | GPIO35, lasten 10 kΩ proti 3,3 V, kontakt proti GND |
| Skupni kontakt stikala | GND; na sredini sta oba kontakta odprta |
| GPS TX → ESP32 RX | 19 |
| GPS RX ← ESP32 TX | 4 |
| IMU SDA / SCL | 21 / 22 |
| SD CS / MOSI / MISO / SCK | 13 / 15 / 2 / 14 |
| TFT CS / DC / RST | 27 / 26 / 25 |
| TFT MOSI / MISO / SCK | 23 / 33 / 18 |
| TFT LED/BL | Zunanje napajanje 3,3 V |

GPIO34/35 nimata notranjih pull-up uporov. Vsak potrebuje svoj upor proti **3,3 V**.
Pri polmetrski žici je zunanji 10 kΩ smiseln tudi za GPIO32/5. Vsi GND so skupni;
sklenjen kontakt pomeni LOW. GPIO16/17 nista uporabljena. GPIO0/12 ostajata
neuporabljena zaradi zagonskih funkcij. GPIO5 po zagonu deluje kot vhod.

NEO-6: nominalno 5 Hz, UART 38400. IMU: nominalno 50 Hz; WHO_AM_I 0x70 pomeni
MPU6500. TFT je nastavljen kot ILI9488, ležeče 480 × 320. USB je 115200 baud.

SD MISO na GPIO2 lahko ovira nalagalni način. Če nalaganje odpove, pri izklopljenem
USB začasno odstrani kartico, priklopi USB in naloži. Za uporabo jo vrni pri
izklopljenem USB. To je obvoz, ki ne potrjuje vzroka vsake napake povezave.
[Espressif: zagonski način](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

## Potrjeno upravljanje

| Dejanje | Rezultat |
| --- | --- |
| Kratek zeleni zunaj nastavitev | Naslednja od treh QUICK INFO strani; povratek po 5 s |
| Rdeči OFF → ON | Omogoči poskus vstopa; meni se še ne odpre |
| Stikalo na sredini + zeleni 2 s | Odpre SETTINGS, če ni aktivnega kroga, sveže hitrosti nad 5 km/h ali nepotrjenega alarma |
| Stikalo levo + zeleni | Ena vrstica gor oziroma manjša številčna vrednost |
| Stikalo desno + zeleni | Ena vrstica dol oziroma večja številčna vrednost |
| Stikalo na sredini + zeleni | Odpre ali potrdi izbrano vrstico; BACK vrne en nivo |
| Držanje smeri in zelenega pri urejanju številke | Ponavljanje po 600 ms; tlak 0,05 bar na 80 ms, ostale številke na 250 ms |
| Rdeči OFF | Izhod in zavrnitev nepotrjenega osnutka |
| Kratek zeleni ob alarmu | Potrditev alarma |

Stikalo samo ne premika izbire; v smeri ga lahko pustiš in klikaš zeleni.
Seznami se ob držanju ne ponavljajo. Premik smeri med držanjem prekine ponavljanje
in ne postane potrditev. Vstop in izhod ne sprožita dodatnega kratkega klika.
Po root BACK je za nov vstop potreben rdeči OFF → ON in nov dolg zeleni.
Zaščita uporablja dejanski krog in dejansko svežo GPS hitrost tudi v razvojnem TEST.
Začetek kroga ali zaznana hitrost nad pragom zapre odprte nastavitve.

## Razpored SETTINGS

Vsak meni, seznam izbir in urejevalnik se odpre z izbranim **BACK** na vrstici 0.
V nadrejenem meniju se po vrnitvi označi postavka, iz katere si prišel. Izbrana
vrstica je rdeča, shranjena možnost zelena. Premikanje izbire ne spremeni shranjene
vrednosti; BACK zavrže nepotrjen osnutek.

```text
BACK
DISPLAY
  BACK / DEFAULT SCREEN / QUICK INFO / BACKGROUND / LAP HOLD / LAP DELTA
  QUICK INFO: BACK / QUICK INFO 1 / QUICK INFO 2 / QUICK INFO 3
  LAP DELTA: BACK / SECONDS PER LIGHT
CALIBRATION
  BACK / IMU CALIBRATE / IMU ZERO
UNITS
  BACK / SPEED / PRESSURE / TEMPERATURE / ACCEL / UTC OFFSET
TRACK
  BACK / TRACK
TPMS
  BACK / PRESSURE ALARM / FRONT MIN/MAX / REAR MIN/MAX / HYSTERESIS /
  ALARM CONFIRM / SENSOR TIMEOUT / FRONT SENSOR ID / REAR SENSOR ID
STORAGE
  BACK / SD LOGGING / SD STATUS / SD FILES / FORMAT SD
```

Številčna nastavitev najprej pokaže BACK in EDIT VALUE. Po izbiri EDIT VALUE
deluje dosedanje hitro nastavljanje. Pri tlaku prvi sredinski pritisk potrdi MIN
in preklopi na MAX; drugi shrani celoten par. Rdeči OFF spremembe zavrže.
BACKGROUND ponudi BACK, BLACK, WHITE in DARK BLUE. Enote in QUICK INFO imajo
isti princip. Obstoječe vrednosti NVS, vključno z `quickSlots` in `deltaIndex`,
ohranijo pomen; stare nastavitve v3 se še vedno prenesejo v v4.

## Primerjalne lučke LAP DELTA

QUICK INFO določa vsebino hitrih strani in ne upravlja lučk. Lučke imajo eno
skupno nastavitev: **DISPLAY → LAP DELTA → SECONDS PER LIGHT**: 0,1 / 0,2 / 0,5 / 1,0 s.
Obe strani zrcalita iste tri stopnje. Število je
`min(3, floor(abs(delta_ms) / step_ms))`, brez zaokroževanja izpisa.

| Korak | 1 stopnja | 2 stopnji | 3 stopnje |
| --- | --- | --- | --- |
| 0,1 s | 0,10 s | 0,20 s | ≥ 0,30 s |
| 0,5 s | 0,50 s | 1,00 s | ≥ 1,50 s |

Pod enim korakom ni prižgane stopnje. Hitreje je zeleno, počasneje rdeče;
pravila so enaka za oba predznaka. Delta primerja kumulativni čas pri S1, S2 in
cilju z isto mejo najboljšega veljavnega celotnega kroga dneva za isto progo.
Med mejami ostane zadnja primerjava. Brez reference je indikator nevtralen;
to ni zvezna napoved iz celotne poti.

## SD in kalibracija

SD LOGGING je privzeto OFF. `set logging on` ga vključi in shrani nastavitev.
REAL se zapisuje v `/sessions/`, razvojni TEST v `/test/`. Zapisi vsebujejo
časovne oznake GPS, surovi IMU, dogodke in geometrijo; napaka SD ne ustavi timinga.
SD FILES pokaže po pet imen datotek in velikosti, BACK je prvi. NEXT/PREVIOUS
PAGE zamenjata stran. Pregled sega do treh nivojev podimenikov in poteka med zapisi.

FORMAT SD ponudi **BACK, NO – KEEP FILES, ERASE ALL FILES**. Samo zadnja izrecna
izbira formatira FAT32 in izbriše vse datoteke, tudi dnevne reference BEST.
Zapisovanje mora biti izklopljeno; motor miruje, brez aktivnega kroga/alarma in
v REAL. Napajanja med formatiranjem ne odklapljaj. Zagon sam kartice ne formatira.

IMU CALIBRATE vodi skozi pokončen položaj, levi in desni nagib. Na vsakem koraku
je najprej izbran BACK; za začetek izberi MEASURE. V pokončnem položaju postavi
motor v želeno ničlo, nato nagni levo in desno za 5–70°. Vsakič meri dve sekundi
in pokaže OK; manjši šum dopušča, dejansko premikanje povzroči ponovitev meritve.
Iz treh položajev določi osi tudi pri zasukanem senzorju. Shrani šele uspešen
celoten postopek. BACK ali rdeči OFF varno prekine zajem in ohrani prejšnjo kalibracijo.
IMU ZERO omogoča poznejšo ločeno nastavitev ničle.

## Prevod, terminal in razvojni build

```powershell
.\.venv\Scripts\platformio.exe run -e esp32dev
.\.venv\Scripts\platformio.exe run -e esp32dev --target upload --upload-port COM4
.\.venv\Scripts\platformio.exe device monitor --port COM4 --baud 115200 --filter send_on_enter --echo
```

Vsak ukaz potrdi z Enter. Ctrl+C zapre monitor; pred nalaganjem ga zapri.
Osnovni ukazi: `help`, `status`, `display status`, `verbose on/off`, `screen race/camera`,
`settings show/reload`, `set ...`, `sd status`, `sd files`, `sd inspect`,
`imu status`, `imu calibrate/next/cancel/zero` in `reference reload`.
`sd format` samo izpiše navodila za potrditev in ne briše kartice.

Razvojna različica zahteva izrecno okolje `esp32dev-dev` (`LAP_TIMER_DEV=1`):

```powershell
.\.venv\Scripts\platformio.exe run -e esp32dev-dev
.\.venv\Scripts\python.exe tools/test_storage_imu.py --suite settings
```

Samo v razvojnem buildu so dostopni `selftest`, `selftest settings`,
`selftest storage-imu`, simulator `test ...`, simulacija kontaktov `button ...`
ter diagnostični `key ...` in `switch ...`. Testni viri so ohranjeni; stari demo
zasloni, zeleni preview in enoznakovni legacy ukazi so odstranjeni.
Podrobnosti: [terminalski vodič](docs/codex_terminal_test.md).

## Preverjanje te spremembe

Prevod običajne in razvojne različice je uspešen. Ciljni preizkus nastavitev:
**34 uspešnih, 0 neuspešnih** preverjanj BACK,
QUICK INFO, shranjenih vrednosti, številčnega ponavljanja in pragov lučk.
NVS shema/zapisovalnik, GPS nastavitve, timing/proga, IMU/kalibracija ter fizični
GPIO zajem so primerjani z izhodiščem in ostajajo nespremenjeni.

Iz običajne različice so odstranjeni nadomeščeni per-GGA CSV/readback, stari
demo zasloni, zeleni preview, podvojeno legacy upravljanje in odslužena statistika.
Testni viri ostajajo dostopni v razvojnem buildu. Poraba spodaj je izmerjena za
celoten firmware in vključuje tudi izločitev razvojnih funkcij iz običajnega builda.

| Build | Flash aplikacije | Statični RAM |
| --- | --- | --- |
| Izhodišče 2488d62 z razvojnimi funkcijami | 719054 B | 52840 B |
| Običajni esp32dev | 644278 B (49,2 %) | 52544 B (16,0 %) |
| Razvojni esp32dev-dev | 707254 B (54,0 %) | 52560 B (16,0 %) |

Običajni build prihrani **74776 B flasha in 296 B statičnega RAM-a** glede na
izmerjeno izhodišče. Dinamična poraba nalog in medpomnilnikov ni vključena v
statični RAM izpisa prevajalnika. Celotnih strojnih/serijskih sklopov pri tej
spremembi ne ponavljamo; ne formatiramo kartice in ne spreminjamo kalibracije.

## Omejitve

**Znana napaka optimizirane različice:** po uporabnikovem preizkusu se zaslon
CAMERA prikazuje enako kot RACE. Razlikovanje prikazov je treba še popraviti.

Sprejem BLE TPMS, merjenje baterije in vključitev pospeška IMU v interpolacijo
prečkanja črte še niso implementirani. Dinamični nagib ostaja eksperimentalni:
vibracije, pospeševanje, zdrs, naklon ceste in časovna poravnava GNSS ga omejujejo.
GNSS nima PPS sinhronizacije. Fizična nova kalibracija in formatiranje SD še nista
potrjena na napravi. Običajna različica je bila 26. 10. 2026 uspešno naložena
na COM4 brez SD kartice; zapis je preverjen s kontrolno vsoto, zagon in odgovor
na `status` sta potrjena. Nastavitve NVS so ohranjene. Za uporabo SD jo vrni
pri odklopljenem USB in ploščico znova zaženi.

Pomposa Long uporablja zgodovinski AiM PomposaMotoL. Sektorski meji sta projektni
tretjini trase, ne uradni AiM točki; ujemanje s trenutno Solo 2 konfiguracijo ni
potrjeno. [Izvor in geometrija](tracks/pomposa_long.json); ponovljiv uvoz:
`python tools/import_aim_pomposa.py`.
