# LAP TIMER Q0 — ZA PREDRAČUN, NE ZA IZDELAVO

## Kaj je pripravljeno
- Native KiCad 7 projekt: PCB, projektna datoteka in arhitekturna skica v .kicad_sch.
- 80 x 65 mm pravokotni obris, štiri bakrene plasti, nominalno 1,6 mm, štiri predhodne M3 luknje.
- Postavljeni kandidatni odtisi ESP32-S3, IMU, USB-C, SD, konektorjev in vzorčnih pasivnih elementov.
- GNSS QUESCAN in CC1101 sta ločena modula, ne dodatna GNSS/RF čipa na tej plošči.
- Gerberji in Excellon svedri, izvoženi s KiCad 7 iz priložene PCB datoteke.
- Proračunski seznam delov in popis postavitve. Nista proizvodna BOM/CPL.
- Dodatek Q0.1: v pcba_quote_subset/ sta DELNI BOM in CPL samo za U1/U2 za poskus uvoza in delni predračun sestave; ne za naročilo.

## Bistvene omejitve
To je začetni študijski projekt za ceno GOLE PLOŠČICE. Ni funkcionalen električni načrt:
ni netliste, povezav, napajalnega vezja ali dokončno preverjenih odtisov/pinoutov.
Kondenzatorji in upori so vzorčna gostota postavitve, ne določena električna vezava.
Schematic je arhitekturna skica z grafičnimi bloki, ne električna shema.
Ni preverjeno prileganje v ohišje/zaslon, RF zmogljivost ali ustreznost napajanja.
Noben rezultat geometrijskega pregleda ne pomeni potrjene električne pravilnosti.
NE naroči izdelave ali sestave teh datotek. Plošča brez povezav ne bo delovala.

## Okvirni predračun na JLCPCB
https://jlcpcb.com/ — PCB / Instant Quote, naloži ločeni GERBERS_ONLY_QUOTE.zip.
Če želiš samo okvirno ceno, ni treba oddati ali plačati naročila.
Nastavitve: FR-4, 4 layers, 80 x 65 mm, 1.6 mm, 1 oz zunanjega bakra,
5 kosov, zelen solder mask, bel silkscreen, lead-free HASL, brez zlatih prstov,
brez slepih lukenj in brez naročene kontrole impedance.
To so le ponudbene nastavitve; končni stackup in napajanje določimo pozneje.
Prototip z ročnim spajkanjem je mogoče primerjati tudi z ENIG doplačilom.

Za ZANESLJIVO ceno PCBA z elementi potrebujemo še dokončno shemo, dejansko BOM
z dobavljivimi šiframi, preverjene odtise in CPL. Priloženega popisa ne uporabi kot CPL.
Za grobo ponudbo PCBA lahko prodajalcu pošlješ ta paket z izrecnim pojasnilom statusa.
QUESCAN modul ni nujno v knjižnici njihove avtomatske sestave.

## Naslednja revizija
Potrdimo točno izvedbo ESP32-S3/temperaturni razred, mere zaslona, QUESCAN/CC1101
ploščic in konektorjev, kapaciteto/polariteto baterije, napajalno arhitekturo,
luknje in ohišje. Nato električna shema, odtisi, povezave in dejanska preverjanja.
Ne kopiraj starega GPIO razporeda ESP32-WROOM neposredno na S3.
Zaslon/GNSS/CC1101 niso vključeni kot avtomatsko sestavljeni deli te plošče.

## Delni PCBA uvoz (dodatek Q0.1)
Za ESP32 in IMU sta zdaj pripravljena BOM/CPL v pcba_quote_subset/.
Preberi tamkajšnji README: cena zajema samo dva dela, ne celotne naprave.
Prvotni Gerber ZIP vsebuje isti nespremenjeni osnutek brez povezav.
