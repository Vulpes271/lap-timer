# Codex: testna različica z upravljanjem prek terminala

Status: navodila za implementacijo, NE že izvedene funkcije.
Zahteva uporabnika: 2026-10-24.

## Naloga in obseg

Implementiraj spodnjo testno različico v projektu lap-timer. Najprej preberi
trenutno kodo, README in morebitna AGENTS.md. Ohrani delujočo strojno opremo,
zaslon, GPS, IMU, timing in obstoječe teste. Pomposa in trije sektorji so že
dodani; nadgradi obstoječe module.

PRVA RAZLIČICA MORA BITI V CELOTI UPRAVLJIVA PREK USB TERMINALA.
Uporabnik bo šele po tem montiral fizične gumbe. V tej fazi ne spreminjaj
ožičenja, ne izbiraj dokončnih GPIO in ne zahtevaj priklopljenega stikala.
Implementiraj skupne logične dogodke, ki jih pozneje sprožijo tudi GPIO.
Ne ustavi se pri načrtu: izvedi testno implementacijo, prevedi, preveri in
dokumentiraj dejansko delujoče ukaze. Omejitve fizičnega preverjanja jasno navedi.

CAMERA pomeni prikaz za snemanje z zunanjo kamero, ne snemanja videa z ESP32.
Izbira zaslona ne ustavlja zajema, timinga ali SD.
Simulator mora biti jasno ločen od dejanske vožnje.

## 1. Terminal kot nadomestilo celotnega krmilnega stikala

Predvideno stikalo ima:
- QUICK: trenutni gumb;
- SETTINGS: zaskočni ON/OFF;
- LEFT / CENTER / RIGHT: tripozicijsko stikalo.

Razporeditev njegovih sedmih žic še ni izmerjena. Ne ugibaj barv ali kontaktov.

Dodaj logične dogodke in terminalski adapter. Predlagan besedilni protokol,
vsak ukaz potrjen z Enter:
- help
- status
- key quick
- switch settings on
- switch settings off
- switch direction left
- switch direction center
- switch direction right
- screen race
- screen camera

Ukazi switch morajo simulirati resnično stanje: ponovitev že aktivnega stanja
ne ustvari novega roba. LEFT/RIGHT izvedeta en korak, naslednji zahteva vrnitev
na CENTER. Dodaj tudi priročna ukaza key left/key right, ki ustvarita celoten
premik iz sredine in nazaj, če to olajša testiranje.

Ohrani obstoječe serijske ukaze, kjer ne nasprotujejo novemu protokolu.
Ne razčlenjuj črk znotraj novih besed kot starih enoznakovnih ukazov.
Če brezhibna soobstojnost ni mogoča, jasno uvedi način legacy in dokumentiraj
prehod. Bralnik mora biti neblokirajoč, imeti omejeno dolžino vrstice in
obravnavati CR/LF, napačne ukaze ter preliv. Potrditve naj povedo končno stanje.

Vožnja:
- LEFT izbere RACE, RIGHT CAMERA, CENTER ohrani izbrano.
- SETTINGS ON odpre nastavitve, OFF jih zapre.
Nastavitve:
- LEFT/RIGHT izbirata postavko.
- QUICK odpre urejanje, LEFT/RIGHT spremenita vrednost, QUICK potrdi.
- Podmeniji imajo NAZAJ.
- SETTINGS OFF zavrže samo nepotrjeno urejanje.
- Brez samodejnega ponavljanja ob držanju stikala.

## 2. RACE, CAMERA in hitri pregledi

RACE: predvsem velik čas kroga, številka kroga, sektorska primerjava,
majhna statusna vrstica GPS/SD; tlak lahko manjši.

CAMERA: velik nagib, smer levo/desno, hitrost, vzdolžni pospešek/pojemek.
Čitljivo in stabilno za zunanjo kamero. Nekakovostne meritve označi.

QUICK med vožnjo kroži:
LAST LAP -> REAR PRESSURE -> FRONT PRESSURE -> LAST LAP.
Vsaka stran ima zelo veliko glavno vrednost.
Po 5 s brez pritiska se vrne osnovni zaslon, aktiven pred pregledom.
Vsak pritisk obnovi rok. Pred prvim krogom LAST LAP kaže --.
Brez senzorja tlak kaže --, nikoli 0. Zastarel podatek označi.

## 3. Ciljna črta, LAP HOLD in nov BEST

Ob prehodu cilja se naslednji krog začne takoj z interpoliranim GPS časom.
Zadrži SAMO PRIKAZ končnega časa prejšnjega kroga:
- LAP HOLD privzeto 5 s, nastavljivo 5–15 s;
- nato pokaži že tekoči čas naslednjega kroga.
Sprožilec je ciljna črta, ne zaznavanje ravnine.
Brez blokirajočih delay().

Ob novem najboljšem veljavnem času dneva:
- polno zeleno ozadje, zelo velik čas, oznaka BEST;
- traja LAP HOLD;
- merjenje se nadaljuje.
Prvi veljaven krog postavi referenco, ni izboljšanje prejšnjega rekorda.

BEST je najboljši celoten veljaven krog istega dne in iste konfiguracije piste,
ne sestavljen idealni krog. Ohrani ga čez ponovni zagon prek SD.
Uporabi preverjen GNSS datum in nastavljiv lokalni časovni odmik.
Brez veljavnega datuma uporabi jasno ločeno referenco seje.
Shrani različico/geometrijo piste; ne mešaj referenc različnih konfiguracij.

## 4. Pomposa in tri sektorske primerjave

Uporabi obstoječi katalog in tracks/pomposa_long.json.
Ohrani zapis izvora: start/cilj je iz zgodovinskega AiM izvoza PomposaMotoL,
sektorski meji sta projektno izpeljani pri tretjinah trase in nista uradni
AiM sektorski meji. Ujemanje s trenutnim Solo 2 izvozom še ni potrjeno.

Na S1/S2 primerjaj kumulativni čas od starta z istim mestom referenčnega
najboljšega kroga dneva. Na cilju primerjaj celoten krog.
Posamezne sektorske čase shrani ločeno za analizo.
Negativna delta zelena, pozitivna rdeča, brez reference siva.
Primerjava ostane do naslednje meje; v novem krogu se ponastavi.

Obstoječi segmenti ob zaslonu prikazujejo velikost delte.
Število sektorjev in število segmentov sta ločena podatka.
DELTA PER SEGMENT: 0.1 / 0.2 / 0.5 / 1.0 s, privzeto 0.2 s.
Uporabi začete korake razlike, omejene na število segmentov, in majhno
dokumentirano nevtralno območje okoli ničle.

Referenca ostane skozi cel trenutni krog enaka. Na cilju najprej primerjaj
s staro, nato po potrebi zamenjaj BEST.
Ohrani interpolacijo, veljavnost, zaporedje sektorjev in zavračanje izpadov.

## 5. Nastavitve

V meniju omogoči:
- osnovni zaslon RACE/CAMERA;
- km/h ali mph;
- tlak bar/psi/kPa;
- temperatura °C/°F;
- pospešek g ali m/s²;
- izbira piste iz kataloga;
- LAP HOLD;
- DELTA PER SEGMENT;
- PRESSURE ALARM ON/OFF;
- FRONT MIN/MAX in REAR MIN/MAX;
- histereza in čas potrditve alarma;
- dodelitev identitet TPMS na F/R;
- kalibracija IMU;
- lokalni odmik časa za dnevno referenco;
- stanje SD in beleženja.

Interno enotne fizikalne enote; sprememba prikaznih enot ne spremeni alarmnih mej.
Potrjene vrednosti shrani v NVS z verzijo in validacijo, ne v vsaki zanki.
MIN mora biti manjši od MAX. Ne izmišljaj priporočenih tlakov.
Alarm je privzeto OFF do nastavitve mej in potrditve dodelitve senzorjev.
Menjava piste zaključi/označi sejo in uporabi ustrezno referenco.

## 6. Alarmi tlaka in prednost prikazov

Svež veljaven tlak pod MIN ali nad MAX sproži cel rdeč zaslon:
velik F/R, DEJANSKI izmerjeni tlak z enoto in manjša oznaka LOW/HIGH.
Primer F 1.70 bar prikazuje izmerjeni sprednji tlak, ne nastavljene meje.

QUICK potrdi alarm in vrne RACE. Isti pritisk ne odpre hitrega pregleda.
Isto trajno odstopanje se ne sme takoj ponovno odpreti, majhen indikator ostane.
Ponovna oborožitev sledi stabilni vrnitvi v dovoljeno območje s histerezo.
Drug senzor ali nasprotna vrsta alarma je nov dogodek.
Če sta aktivni obe pnevmatiki, pokaži obe; QUICK potrdi vse prikazane dogodke.
Manjkajoč/zastarel TPMS je SENSOR LOST, ne nizek tlak.
Obravnavaj dejanski interval oddajanja; stari podatek ni nova meritev.

Prednost:
1. nov nepotrjen alarm;
2. SETTINGS, kadar je vklopljen;
3. hitri pregled;
4. BEST/LAP HOLD;
5. osnovni zaslon.

Roki tečejo od izvornega dogodka tudi med prekritjem; ne pokaži pretečenega
BEST naknadno. Alarm ne izgine zaradi izteka hitrega pregleda.
Če QUICK potrdi alarm ob SETTINGS ON, vrni RACE in zahtevaj OFF->ON,
preden se nastavitve znova odprejo.

Notranji BLE TPMS model še ni dokončno izbran. Pripravi vmesnik in simulator,
ne trdi, da je povezava z dejanskim senzorjem preverjena.

## 7. IMU: ničla, orientacija in dinamični podatki

Loči:
- bias žiroskopa pri preverjenem mirovanju;
- rotacijo senzor->motor;
- uporabniško ničlo nagiba.

Čarovnik:
- motor pokonci na približno ravni podlagi, mirujoč zajem;
- znan nagib v levo za določitev osi/predznaka;
- določitev in preverjanje smeri naprej; omogoči tudi ročno montažno orientacijo.
Statična ničla sama ne določa smeri naprej.
Preveri kakovost, pravokotnost in predznake; zavrni nestabilen zajem.
Ne zahtevaj fizičnega nagiba motorja za 90 stopinj.
Shrani pravo rotacijo vseh treh osi, ne samo odmika ene številke.
Dokumentiraj konvencijo osi in postopek.

Trenutni atan2(ay, az) ni dinamični nagib motorja.
Vzpostavi branje IMU z dejanskim dt neodvisno od zaslona in GPS.
Za fuzijo upoštevaj žiroskop, pospeškomer in dinamične pospeške.
GNSS pomoč mora upoštevati časovno usklajeno hitrost IN spremembo smeri.
Sama skalarna hitrost ni dovolj za odstranitev bočnih sil.
Surovi gz nagnjenega IMU ni neposredno hitrost vrtenja smeri okoli navpičnice.
Običajen Madgwick/Mahony brez pomoči ni dokaz rešene napake v dolgem ovinku.
Formula ravnovesnega ovinka ne sme biti edina meritev nagiba.

Vzdolžni pospešek/pojemek izpelji iz transformiranih IMU podatkov z odstranjeno
gravitacijo. GNSS sprememba hitrosti je počasnejša kontrola/pomoč.
Dokumentiraj omejitve: naklon ceste, vibracije, zdrs, zakasnitev, izguba GNSS.
Izpis mora vključevati veljavnost/kakovost. Ne skrivaj napak z glajenjem.
Če celotna dinamična rešitev še ni terensko preverjena, jo označi experimental
in ohrani surove podatke za nadaljnji razvoj. To ne sme blokirati ostalih faz.

## 8. SD za analizo

Seje poimenuj zaporedno brez prepisovanja.
Shrani:
- firmware, čas, pisto in geometrijo vseh črt, nastavitve, kalibracijo;
- GPS čas meritve in lokalni monotoni čas;
- GNSS položaj, hitrost, smer, kakovost, manjkajoče epohe;
- surovi pospeškomer/žiroskop pri dejanski IMU frekvenci;
- orientacijo, nagib, vzdolžni pospešek in kakovost;
- TPMS tlak, temperaturo, baterijo, identiteto in starost podatka;
- prehode črt in interpolirane čase, sektorje, kroge, razlog neveljavnosti;
- BEST/reference in dogodke alarmov ter njihove potrditve.

Vsak tok ima lastne časovne oznake, ne podvajaj starega GPS kot nove meritve.
Uporabi medpomnilnik in ločen zapisovalnik. V normalni vožnji ne odpiraj,
zapiraj in ponovno prebiraj vsake vrstice.
Redno prazni podatke na SD. Beleži izgubljene zapise/preliv.
Napaka SD ne ustavi timinga ali alarma. Dokumentiraj izgubo zadnjega dela
ob nenadnem izklopu. Obstoječi readback test ohrani kot diagnostiko.

## 9. Izrecni terminalski TEST MODE

Za preverjanje pred montažo gumbov in nakupom TPMS dodaj:
- test on / test off;
- test pressure front <vrednost_bar>;
- test pressure rear <vrednost_bar>;
- test sensor front lost / test sensor rear lost;
- test lap <čas_ms>;
- test sector <1|2> <kumulativni_čas_ms>;
- test reset.

Navedena sintaksa je predlog, lahko jo smiselno prilagodiš, toda dokumentiraj
točne ukaze. Testni dogodki morajo uporabiti isti UI/alarmni avtomat kot resnični.
Za simulacijo LAP/BEST uporabi izoliran vir dogodkov/sejo, da ne pokvariš
dejanskega timing stanja. Ne zamenjaj s sintetičnim GPS replay testom geometrije.
Onemogoči testno vbrizgavanje, dokler test mode ni izrecno vključen.
TEST mora biti stalno očitno označen na zaslonu in v terminalu.
Testni krogi, tlaki in reference ne smejo postati dejanski BEST ali zapis
dejanske seje. Testne datoteke/metapodatki morajo biti ločeni in označeni.
TEST OFF obnovi dejanske vire, počisti testne alarme/preglede/reference in
ne pusti simuliranih vrednosti videti kot resnične.

Simulator mora omogočiti:
- poln meni brez stikal;
- preklop RACE/CAMERA in hitrih pregledov;
- običajen končan krog in nov BEST;
- S1/S2 primerjavo z isto referenco;
- LOW/HIGH na F in R, oba alarma, potrditev in ponovno oborožitev;
- manjkajoč senzor in iztek podatkov;
- preverjanje pretvorbe enot ter trajnih nastavitev.

## 10. Faze, testi in dostava

Izvedi po fazah:
1. skupni dogodki, terminal, meniji, NVS;
2. RACE/CAMERA, hitri pregledi, LAP HOLD/BEST;
3. dnevna referenca in sektorji;
4. TPMS vmesnik, alarmi, testni viri;
5. SD;
6. kalibracija in dinamična IMU ocena.

Napiši smiselne teste avtomatov in pretvorb:
- QUICK med alarmom ne odpre pregleda;
- petsekundni timeout in njegovo podaljšanje;
- nov cilj/BEST med drugim prikazom;
- oba tlaka in ponovna oborožitev;
- enote ne spremenijo fizikalnih mej;
- ponovno nalaganje nastavitev;
- menjava piste/dneva;
- testni viri ne onesnažijo dejanske seje;
- izguba GPS/IMU/TPMS in napaka/počasnost SD;
- ponovljena stanja stikala ne ustvarijo novih pritiskov;
- novi besedilni ukazi ne sprožijo legacy črk.

Ohrani in zaženi obstoječe timing teste. Prevedi za obstoječi ESP32.
Posodobi README: serijski monitor 115200, točni ukazi in kratek scenarij
za ročno preverjanje od zagona do alarma/BEST/SD.
Če je COM4 na uporabnikovem lokalnem računalniku dejansko na voljo, preveri
tam; sicer ne trdi, da je bilo nalaganje ali fizični test opravljen.
Uporabniku izpiši jasen vrstni red ukazov, ki jih kopira v terminal.

Na koncu poročaj: spremembe, rezultat prevajanja/testov, dejanske omejitve,
kaj ostaja za fizično preverjanje in prihodnjo montažo gumbov.
