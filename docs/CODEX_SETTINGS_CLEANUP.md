# Codex: čiščenje projekta in intuitiven SETTINGS

Naloga 2026-10-26. Pregledana osnova: commit 2488d62.
Ta dokument dopolnjuje oziroma pri spodnjih točkah nadomesti stare razvojne
načrte. Izvedi popravke, ne nove implementacije že delujočih funkcij.

## 1. Najprej očisti razvojne ostanke

Ohrani RACE/CAMERA, fizične gumbe in njihove zaščite, hitre preglede, timing,
Pomposo/sektorje, dnevni BEST, alarme, IMU/kalibracijo, NVS in SD analizo.
Ohrani trenutno potrjeno logiko upravljanja: LEFT/RIGHT + GREEN premika,
CENTER + GREEN izbira; vstop RED + dolg GREEN. Ne spreminjaj GPIO.

V src/main.cpp odstrani nadomeščeni per-GGA CSV/readback zapis
(beginLog/logGpsEpoch in pripadajoče stanje), če je potrjeno neuporabljen;
aktivni analysis_logger.h ostane. Odstrani pripadajoči zavajajoči
STATIONARY_TEST način, ne zajema IMU/GPS ali potrebne diagnostike.

Stare demo zaslone, greenPreview, legacy podvojeno upravljanje in odslužene
statistične izpise odstrani iz običajne različice, če jih aktivne funkcije
ne potrebujejo. Selftest/simulator in še uporabne razvojne pripomočke prestavi
za izrecni build flag oziroma v ločeno razvojno okolje, privzeto izklopljeno;
ne briši uporabnih testnih virov samo zaradi velikosti firmware.
Ohrani osnovni terminal za status, nastavitve in diagnostiko.
Odstrani res nepovezano/mrtvo kodo in podvojene zgodovinske opise.
README naj opisuje trenutno stanje; zgodovino hrani Git.
Ne izvajaj splošnega prepisovanja arhitekture.

## 2. BACK mora biti prvi in takoj izbran

V vsakem novoodprtem meniju/podmeniju/seznamu izbir je vrstica 0 BACK,
ki je ob vstopu tudi označena. Tako takojšnji CENTER + GREEN vrne en nivo.
Velja tudi za SD FILES in menije posameznih nastavitev.
Ob povratku v nadrejeni meni označi postavko, iz katere smo prišli.
Root BACK zapre SETTINGS z obstoječim ponovnim odklepanjem RED OFF/ON.

Loči indeks vrstice od vrednosti nastavitve: premik BACK ne sme premakniti
enot, proge ali drugih shranjenih vrednosti. Trenutno vrednost še vedno označi.
BACK zavrže nepotrjen osnutek. Pri številčnih urejevalnikih dodaj takoj dostopni
BACK/CANCEL pred urejanjem vrednosti; ohrani hitro ponavljanje med urejanjem.
Izhod/preklic IMU čarovnika ohrani prejšnjo kalibracijo; varno prekini zajem.
Ne odstrani potrditve formatiranja SD.

Popravi categoryItem/treeShort, izbore choiceLabel/treeLong in SD fileCursor
ter prikaz skupaj, ne samo vrstnega reda besedil.

## 3. GREEN 1/2/3 niso lučke primerjave

Trenutni Quick1/Quick2/Quick3 v control_model.h določajo vsebino hitrih
informacijskih strani. Ohranijo naj funkcijo, vendar jih preimenuj v
QUICK INFO 1/2/3 in združi pod QUICK INFO; brez imen GREEN 1/2/3.
Ohrani njihove shranjene vrednosti in petsekundni povratek.

Za primerjalne lučke je samo ENA nastavitev:
LAP DELTA > SECONDS PER LIGHT: 0.1 / 0.2 / 0.5 / 1.0 s.
To velja skupno za vse tri stopnje na obeh straneh zaslona.
Leva in desna stran zrcalita isti indikator; to ni šest stopenj ali
ločeno upravljanje posamezne lučke.

delta = trenutni kumulativni čas - čas na isti meji najboljšega veljavnega
celotnega kroga DNEVA za isto pisto. Hitreje zeleno, počasneje rdeče.
Ohrani obstoječo sektorsko primerjavo: S1, S2 in cilj; med mejami ostane
zadnja primerjava. Ne predstavljaj tega kot zvezne napovedi brez reference poti.

Število prižganih stopenj = min(3, floor(abs(delta) / korak)).
Uporabi interno milisekunde, ne zaokroženega izpisa ali ceil:
- korak 0.1 s: 0.10 -> 1, 0.20 -> 2, 0.30 ali več -> 3;
- korak 0.5 s: 0.50 -> 1, 1.00 -> 2, 1.50 ali več -> 3.
Pod enim korakom nobena; brez reference/veljavnosti nevtralno.
Enaka pravila za negativno in pozitivno delto.
Odpravi podvojen izračun v Model::segments in Dashboard::updateLapTimer,
da uporablja en skupni izračun. NVS deltaIndex prenesi brez izgube nastavitev.

## 4. Zaključi brez nepotrebnih ponovitev

Ne ponavljaj potrjenih GPS/IMU/SD strojnih testov ali celotnih serijskih
scenarijev. Prevedi običajno različico; če ohraniš razvojni build, tudi tega.
Preveri samo spremenjeno navigacijo BACK, pomen QUICK INFO, navedene mejne
vrednosti indikatorja in ohranitev nastavitev. Brez novih obsežnih testnih paketov.
Ne formatiraj SD in ne ponavljaj kalibracije zaradi te naloge.

Na koncu kratko navedi odstranjene dele, spremembe SETTINGS in dejanski rezultat
prevajanja. Pri čiščenju loči prihranek izvornih datotek od izmerjenega
prihranka flash/RAM; ne trdi, da izbris komentarjev zmanjša firmware.
