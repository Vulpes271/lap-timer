# Test timer na samostojnem ESP32

Ločen PlatformIO projekt za preizkus Android aplikacije v `race/app`. Potrebuje
samo ESP32 z napajanjem prek USB. Takoj po zagonu simulira vrnitev v bokse:
ponudi zaščiten Wi-Fi in eno že zaključeno vožnjo. GPS, zaslon, SD in tipala niso potrebni.

| Podatek | Vrednost |
| --- | --- |
| Wi-Fi SSID | **LapTimer-TEST** |
| Geslo | **testtimer123** |
| Naslov | **192.168.4.1** |
| Ime steze | **TEST - Pomposa** |
| Vožnje | **1** |
| Krogi | **8**, vsak s tremi sektorji |
| Najboljši krog | **1:03.800**, krog 6 |
| Identiteta | `TEST-` in MAC čipa; ločena od pravega merilnika |

Datum in čas sta zapisana v `demo-session.json` ter `include/demo_data.h`.
Pripravljena vožnja je za **22. 11. 2026**. Ponovni vklop ali ponovna povezava
ponudita isti ID vožnje, zato običajen ponovni prenos ne ustvarja novih voženj.
Podatki ostanejo v programu tudi po izklopu USB.

## Uporaba na telefonu

1. ESP32 priklopi na USB in počakaj nekaj sekund, da ponudi `LapTimer-TEST`.
2. V aplikaciji odpri **Nastavitve**, vključi **Uporabljam svoj timer** in izberi
   **Povezava z merilnikom**. Odpre se zaslon **Časi z naprave**.
3. Vnesi `LapTimer-TEST` in geslo `testtimer123`, nato pritisni **Poveži napravo**.
   Dovoli zahtevana dovoljenja in potrdi Androidovo povezavo. Omrežje nima interneta.
4. Nova aplikacija po uspešnem prenosu odpre **Prenesi vožnjo v dan**. Izberi dan
   **22. 11. 2026**, voznika/motor in novo ali obstoječo vožnjo ter potrdi.
   Če dneva še ni, izberi **Nov dan iz te vožnje**, motor in voznika ter potrdi.
   Pri novi vožnji lahko gumi ostaneta neznani. Po uspehu se odpre izbrani dan.
5. Če izbereš **Pozneje**, na zaslonu povezave uporabi **Prenesi v dan**.
   Druga možnost ostaja **Danes → Pripni čase z merilnika**. Za pregled samih
   krogov izberi **Session 1 · TEST - Pomposa** in **Pokaži kroge in sektorje**.
6. Preveri 8 krogov, najboljši čas `1:03.800` in podrobnosti sektorjev.
   Ponovi prenos in preveri, da ista vožnja ni podvojena. Časi so po prenosu dostopni tudi brez povezave.

Za preizkus drug dan je treba pripraviti nov datum in znova naložiti program;
aplikacija pri pripisu filtrira vožnje po datumu dogodka.

## Gradnja in nalaganje

Ukazi iz korena `lap timer`, kjer že obstajata `.venv` in `.platformio`:

```powershell
# Pripravi eno vožnjo s trenutnim ljubljanskim datumom in začetkom pred 20 minutami.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\test timer\prepare_demo.ps1"

# Prevedi. Za ta projekt ni zunanjih knjižnic; uporablja nameščeni ESP32 Arduino SDK.
.\.venv\Scripts\platformio.exe run --project-dir ".\test timer"

# Zamenja program na priklopljenem ESP32 s simulatorjem.
.\.venv\Scripts\platformio.exe run --project-dir ".\test timer" --target upload --upload-port COM3

# Preveri dejanski odziv simulatorja in HTTP poti prek USB.
.\.venv\Scripts\python.exe ".\test timer\verify_device.py" --port COM3

# Preveri samo skladnost pripravljenih podatkov, ko ESP32 ni priklopljen.
.\.venv\Scripts\python.exe ".\test timer\verify_device.py" --fixture-only
```

Pripraviš lahko tudi določen lokalni datum in čas; upošteva se ljubljanski časovni pas:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\test timer\prepare_demo.ps1" -LocalStart "2026-11-22 14:00"
```

`ExecutionPolicy Bypass` velja samo za zagnani proces; trajne nastavitve Windows
se ne spreminjajo. Po pripravi podatkov vedno znova prevedi in naloži. Nova priprava
ustvari nov ID, zato lahko v arhivu telefona ostane tudi prej prenesena testna vožnja.

USB vrata so lahko po ponovnem priklopu drugačna. V PlatformIO lahko odpreš tudi
to mapo kot samostojen projekt. Serijski monitor pred nalaganjem in preverjanjem zapri.

## Ukazi prek USB

115200 baud, zaključek vrstice LF ali CRLF:

| Ukaz | Pomen |
| --- | --- |
| `status` | Stanje Wi-Fi, število povezanih odjemalcev in HTTP zahtev |
| `sessions` | JSON seznama voženj, ki ga dobi aplikacija |
| `laps` | JSON vseh krogov in sektorjev ene vožnje |
| `depart` | Simuliraj odhod na stezo: izklopi Wi-Fi |
| `return` | Simuliraj vrnitev: vklopi isti Wi-Fi in ponudi isto vožnjo |
| `selftest` | Preizkusi dejanske HTTP poti prek lokalnega TCP/IP sklada ESP32 |
| `help` | Izpis razpoložljivih ukazov |

Za ukaz brez resetiranja ploščice lahko uporabiš tudi:

```powershell
.\.venv\Scripts\python.exe ".\test timer\probe_device.py" status --port COM3
```

Samopreizkus preveri veljavna `/api/v1/sessions` in `/api/v1/laps?id=...`,
manjkajoč/neveljaven ID ter neobstoječo vožnjo in pot. `verify_device.py` preveri
še obliko podatkov, 64-bitni čas, število krogov, seštevke sektorjev ter ohranitev
identitete ob odhodu in vrnitvi. To ne nadomesti povezave z dejanskim telefonom.

Wi-Fi je za preizkus privzeto stalno vključen. Lučka ploščice ni del simulatorja;
stanje preveri prek USB izpisa ali prisotnosti omrežja. Za pregled v brskalniku
na povezanem telefonu odpri `http://192.168.4.1/`.

Program ponuja samo podatke obstoječega protokola: datum, stezo, ID vožnje,
čase krogov in sektorjev. Tlaki, kilometri in GPS sled niso simulirani.

## Opravljeno preverjanje 22. 11. 2026

Gradnja z `platformio run --project-dir ".\test timer"` je uspela. Program uporablja
47.576 B RAM in 1.015.480 B programske particije. Preverjanje `--fixture-only` je
potrdilo ujemanje datoteke JSON in podatkov v programu ter zahteve protokola aplikacije.
Po odpravljeni težavi USB povezave je simulator **naložen na ESP32-D0WD-V3,
COM3, MAC `1C:69:20:C6:DD:18`**. Nalaganje je uspelo in zapisana vsebina je bila
preverjena s kontrolno vsoto. `verify_device.py --port COM3` je nato uspel na
dejanski ploščici: potrdil je vklop Wi-Fi, eno vožnjo, osem krogov, vse sektorje,
obe veljavni HTTP poti in štiri napačne zahteve, izklop/vklop Wi-Fi ter
nespremenjeno identiteto vožnje po vrnitvi. HTTP samopreizkus uporablja TCP/IP
loopback. Uporabnik je nato na telefonu potrdil, da je omrežje **LapTimer-TEST**
vidno. Dejanski prenos in pripis vožnje v Android aplikaciji še zahtevata preizkus
na telefonu po zgornjih korakih. ESP32 mora med uporabo ostati napajan prek USB.
