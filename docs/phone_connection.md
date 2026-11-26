# Povezava Android aplikacije z merilnikom

Za napravo se uporablja **korenski** `platformio.ini` in `src/main.cpp`.
Mapa `race/timing` je druga, starejša osnova; tega firmwarea za povezavo ne nalagaj.
Predhodni aktivni Wi-Fi scanner je ohranjen v `tools/wifi_scan_test.cpp`.

## Prva uporaba

1. Prevedi in naloži korenski firmware z običajnim PlatformIO uploadom.
   Naložiti se mora tudi particijska tabela `huge_app.csv` (standardni upload jo vključi).
   Nova razporeditev ima 3 MiB za program in približno 960 KiB za notranji arhiv;
   OTA ni konfiguriran. NVS nastavitve in SD kartica ostanejo ločene.
2. Na USB terminalu pri 115200 baud preberi zagonsko vrstico `PHONE: SSID=… PASSWORD=…`.
   Naprava ob prvi uporabi ustvari naključno geslo in ga ohrani v svojem NVS prostoru.
3. Namesti Android aplikacijo iz `race/app` in odpri **Časi z naprave · Wi-Fi**.
   Vnesi ime omrežja in geslo iz terminala.
4. Z veljavnim GPS položajem naj motor 30 sekund miruje znotraj 2 m od fiksne točke.
   Naprava tedaj samodejno vklopi zaščiteno omrežje `LapTimer-…` na `192.168.4.1`.
5. Pritisni **Poveži napravo**, dovoli zahtevana dovoljenja in potrdi Androidovo
   povezavo z omrežjem brez interneta. Android 10+ uporabi sistemsko zahtevo za
   konkretno Wi-Fi omrežje. Android 6–9 odpre nastavitve, kjer omrežje izbereš ročno.
6. Izberi **dan**, nato **session**. Vsak veljaven zaključen krog pokaže skupni čas
   in ločene čase S1, S2, S3 v milisekundah.
7. Po uspešnem prenosu aplikacija za še nepripisano vožnjo samodejno odpre
   **Prenesi vožnjo v dan**. Izberi dan, voznika/motor ter novo ali obstoječo vožnjo
   in potrdi. Če dneva še ni, lahko ustvariš nov dan z izbranim motorjem in voznikom.
   Nato se odpre izbrani dan s shranjeno vožnjo in vsemi krogi/sektorji.
   **Pozneje** zapre izbiro; na zaslonu povezave ostane **Prenesi v dan**.

Oko ob geslu omogoča prikaz in ponovno skrivanje vnesenega gesla. Za samostojen
preizkus brez GPS ali tipal je pripravljen projekt [test timer](../test%20timer/README.md).

Po prvi nastavitvi si aplikacija zapomni napravo. Ko odpreš ta zaslon, znova zahteva
povezavo; Android za že odobreno konkretno omrežje lahko povezavo potrdi sam.
Podatki se na odprtem zaslonu osvežujejo vsakih pet sekund. Ob odhodu v ozadje ali
zaprtju zaslona se zahteva sprosti. Aplikacija ne izvaja skritega povezovanja v ozadju.
Če Android zahtevo zavrne ali zaključi brez povezave, je na voljo ponovni **Poveži napravo**.
Android 10–12 zahteva tudi dovoljenje lokacije in lahko zahteva vklop lokacijskih storitev.

## Vožnje in podatki

- Začetek vožnje je prvi kakovosten GPS vzorec s hitrostjo nad 5 km/h po zagonu ali
  potrjenem postanku. Vožnje se samodejno številčijo od 1 za vsak lokalni GPS dan.
- Postanek zahteva 30 s znotraj 2 m od fiksnega sidra in hitrost največ 5 km/h.
  Sidro se ne pomika z vsakim GPS vzorcem. Večji odmik začne odštevanje znova.
- Izguba GPS signala ne šteje kot postanek ali nov izhod. Za nov postanek je treba
  znova zbrati 30 s svežih meritev. Že potrjeno mirovanje ostane potrjeno do zaznanega odhoda.
- Ob potrjenem postanku se nedokončani krog zavrže. Ob odhodu nad 5 km/h se Wi-Fi
  izklopi in začne naslednja vožnja. Krajši postanek ne razdeli vožnje.
- Sprememba dneva ali geometrije proge začne nov arhivski session ob naslednjem
  premikanju. Brez potrjenega datuma se pokaže **Brez GPS datuma**.
- Notranji arhiv se posodobi po vsakem veljavnem zaključenem krogu, tudi če je
  SD beleženje izklopljeno. Nepopolni krogi in razvojni TEST podatki se ne prenašajo.
- Največ 512 krogov na vožnjo. Ob polnem ali okvarjenem notranjem pomnilniku merjenje
  ostane aktivno; prenos pokaže napako. Stari arhiv se ne briše samodejno.
  Inicializira se samo popolnoma prazna notranja particija; okvarjene se ne formatira.
- Telefon hrani prenesene čase lokalno in jih prikazuje tudi brez povezave. Ključ je
  kombinacija identitete naprave in sessiona; večkratni prenos ne podvaja krogov.
- Po potrjenem pripisu se vožnja shrani v Room skupaj s krogi in sektorji ter je
  vključena v JSON backup. CSV izvozi povzetek vožnje. Ročno vneseni tlaki, opombe,
  kilometri, gume in vzmetenje obstoječe vožnje se ohranijo; uporabnik izbere,
  ali uporabi najboljši čas merilnika ali obdrži ročnega.
- Kartica označi že pripisano vožnjo in omogoči **Odpri dan**. Isti ključ naprava/vožnja
  se ne uvozi dvakrat. Sam predpomnilnik še nepripisanih voženj ostaja ločen od JSON backupa.
- Trenutni protokol prenaša čase krogov in sektorjev; tlaki in IMU telemetrija še niso vključeni.

## Protokol in preverjanje

Read-only HTTP API, dosegljiv samo med vključenim Wi-Fi:

- `GET /api/v1/sessions`: verzija protokola 1, identiteta naprave, stanje shranjevanja,
  seznam sessionov (`id`, `day` kot YYYYMMDD, `number`, `startUtcMs`, `track`, `lapCount`).
- `GET /api/v1/laps?id=123`: isti opis sessiona in `laps` z `number`, `lapMs`, `sectorsMs`.

Android usmeri samo te zahteve prek pridobljenega omrežja; ostalih povezav aplikacije
ne preusmeri. HTTP je dovoljen samo za `192.168.4.1`, ostali naslovi zahtevajo HTTPS.
Povezovanje sledi [Android Wi-Fi Network Request API](https://developer.android.com/develop/connectivity/wifi/wifi-bootstrap)
in [dovoljenjem za lokalno omrežje](https://developer.android.com/privacy-and-security/local-network-permission).

`tools/test_paddock.cmd` prevede in izvede preverjanja s nameščenim MSVC 2022.
Za drug C++17 prevajalnik uporabi `tools/test_paddock.cpp`.
Preizkus na dejanski napravi naj preveri 30 s mirovanja, povezavo telefona brez interneta,
dva ločena izhoda, čase treh sektorjev, ponovni prenos brez podvojitev in prikaz po
ponovnem zagonu aplikacije ter naprave. Programsko preverjanje ne nadomesti tega preizkusa.
