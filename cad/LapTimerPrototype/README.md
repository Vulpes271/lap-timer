# PETG prototip nosilca merilnika

Model v1 po uporabnikovih meritvah. Vsi CAD in STL podatki so v **milimetrih**.
Firmware in Android aplikacija pri tej spremembi nista spremenjena.

## Kaj odpreti

- `exports/01_Housing.stl`: glavno ohišje s spodnjim pregibom.
- `exports/02_Front_bezel.stl`: sprednji okvir z ležiščem antene na zadnji strani.
- `exports/03_Electronics_carrier.stl`: odstranljiv pladenj za ESP, GPS in IMU.
- `exports/04_Hinge_tongue_fit_gauge.stl`: **samo merilni kos** za preverjanje vpetja;
  podstavek za pritrditev na motor ni del modela.
- `exports/05_Display_spacer_*mm.stl`: ločeni distančniki za poskusno nastavitev
  odmika zaslona; natisni štiri kose izbrane višine.
- `exports/preview_*.png`: prikazi dejanske geometrije. Zelene/vijolične ploščice
  na razstavljenem prikazu so približne prostorske ovojnice elektronike.
- STEP datoteke, kadar so prisotne, so polna telesa za neposredno odpiranje v Fusionu.
  Zgodovino skic in uporabniške parametre ustvari spodnja Fusion skripta.

## Fusion: urejanje s skicami in parametri

1. Razširi celotno mapo `LapTimerPrototype`, ne samo glavne datoteke Python.
2. V Fusionu odpri **Utilities > Scripts and Add-Ins > Scripts**.
3. Z gumbom za dodajanje skripte oziroma izbiro obstoječe skripte izberi to mapo.
   V njej sta istoimenski `LapTimerPrototype.py` in manifest.
4. Zaženi `LapTimerPrototype`. Skripta ustvari **nov dokument**, ne posega v
   obstoječi odprti model.
5. Mere spreminjaj prek **Modify > Change Parameters**. Ustvarjene so nativne
   skice, izvleki, izvrtine in komponente, ne uvožena STL mreža.
6. Skripta poskusi izvoziti `exports_fusion/LapTimerPrototype.f3d` in STL vsakega
   kosa. Projekt lahko tudi ročno shraniš iz Fusiona.

Fusion v tem delovnem okolju ni nameščen oziroma neposredno povezan. Skripta je
preverjena glede sintakse in uporabljeni API je primerjan z dokumentacijo,
**ni pa bila izvedena znotraj Fusiona**. Če ta pokaže napako, je treba odpraviti
konkretno sporočilo; STEP/STL geometrija je preverjena neodvisno.

Za prihodnje neposredno delo obstaja uradni **Autodesk Fusion MCP Server**.
Fusion mora biti odprt; strežnik vklopiš pod **Preferences > General > API >
Fusion MCP Server**. Privzeti lokalni naslov je `http://127.0.0.1:27182/mcp`.
Nato ga povežeš z odjemalcem, ki podpira MCP prek Streamable HTTP. V tej seji ta
povezava ni vzpostavljena; v dostopnem katalogu Plugin Management ni bil najden
ustrezni vtičnik. Za uporabo priložene skripte povezava MCP ni potrebna.
[Uradna navodila za neposredno povezavo](https://help.autodesk.com/cloudhelp/ENU/ADSKMCP-FusionDesktopMcp/files/ADSKMCP_FusionDesktopMcp_connecting_to_the_fusion_mcp_server_html.html).

## Mere in razporeditev

Osi v sestavu: **X levo–desno, Y spodaj–zgoraj, Z zadnja–sprednja stran**.
Zaslon je ležeč. Zunanje ohišje je **120 × 108 × 55 mm** z nameščenim sprednjim
okvirjem. Pregib sega 31 mm pod spodnji rob, zato je skupna višina **139 mm**.
Osnovne stene in zadnja plošča so 3 mm. Odstranljiv notranji pladenj je 106 × 94 mm.

| Komponenta | Uporabljene mere | Pritrditev / opomba |
| --- | --- | --- |
| Zaslon | 98,3 × 56 × 23 mm | 4 luknje, simetričen razmik 93 × 50 mm; odprtine 3,4 mm za M3 |
| Dejanski zaslon | Izmerjenih **83 × 55 mm**, odprtina **83,4 × 55,4 mm** | 0,2 mm rezerve na stran; predpostavljena sredinska lega na ploščici |
| ESP32 WROOVER-E | 65 × 26 × 13 mm | Podprt 3 mm nad pladnjem, pritrjen z dvema 2,5-mm vezicama; drobne luknje niso ugibane |
| ESP SD priključek | dodatnih 10 mm v desno | Prostor ob ploščici in velika desna servisna odprtina |
| IMU | 26,3 × 16 mm | Razmik 21 mm; vrsta lukenj predvidena 2,5 mm od roba nasproti kablom, reži dopuščata ±1,8 mm premika |
| GPS GY-NEO6MU2 | **36 × 26 × 5 mm** | Popravljeni razmik **31 × 21 mm**, predviden simetrično; M3 nosilci |
| GPS antena | 35 × 35 × **6,7 mm skupne debeline** | Ležišče nad zaslonom, odprto z leve strani za vstavljanje; odmik 0,5 mm po robovih |
| Stikala | uporabnikov kabel Ø6 mm | Ločen izhod Ø7 mm spodaj levo |
| Napajanje | premer kabla še ni podan | Začetni ločen izhod Ø7 mm spodaj desno; notranja baterija ni modelirana |

Pod GPS je prostor za njegove priključke in pot kabla; pri levi/desni orientaciji
modula upoštevaj dejansko mesto antenskega priključka. Za 8-mm kabelsko rezervo
je na levi in desni strani modula več prostora. Za IMU ostane vsaj 8 mm pred robom
s priključki. Električna izolacija in oblika spajkanih priključkov zahtevata fizični pregled.

Med podano 13-mm ovojnico ESP in podano 23-mm ovojnico zaslona so **4 mm rezerve**.
Neposredna pot antenskega kabla je približno 29,3 mm; pri preverjanju je dodana
15-mm rezerva za ovinke. Dejanski antenski priključek in polmer upogiba je treba
preveriti na modulu, kabel je dolg 70 mm.

## Spodnji pregib za tvoj podstavek

To je **lasten večji vmesnik**, ni združljiv s standardnim GoPro vpetjem.

| Mera | Vrednost |
| --- | --- |
| Smer vijaka | Vodoravno, vzdolž X: levo–desno gledano v zaslon |
| Vijak | M6, izvrtina 6,4 mm |
| Debelina vsakega od obeh ušes | 8 mm |
| Prostor med ušesoma | 14,6 mm |
| Debelina jezika na tvojem podstavku | 14 mm, 0,3 mm zračnosti na stran |
| Skupna širina obeh ušes in reže | 30,6 mm |
| Zunanji premer zaobljenega dela | 26 mm |
| Središče osi | X = 60 mm, Y = −18 mm, Z = 26 mm |
| Žep matice M6 | 10,3 mm čez ploskvi, globina 5 mm |

Uporabi kovinski vijak M6 približno 45 mm, ustrezno matico in kovinske podložke;
končno dolžino preveri glede na izbrane podložke in tip matice. Merilni jezik ni
podstavek in nima pritrdilnih lukenj za motor. Sledi samo geometriji tvojega
prihodnjega podstavka: zaobljen jezik 14 mm s prečno luknjo 6,4 mm.

Preverjanje trkov vključuje merilni jezik proti ohišju pri −90, −60, −45, 0, 45,
60 in 90 stopinjah. Ne vključuje oblike prihodnjega podstavka, glave vijaka, kabla
in motorja. To je preverjanje geometrije, **ne nosilnosti ali odpornosti na vibracije**.

## Sestavljanje in PETG tisk

Najprej preveri vpetje z majhnim merilnim kosom ter ujemanje zaslona z okvirjem.
Podana skupna debelina zaslona ne določa višine njegove pritrdilne ploščice glede
na steklo. Za ta odmik so priloženi distančniki 2, 4 in 6 mm; pravo kombinacijo
izberi po dejanskem modulu, pred pritrjevanjem elektronike.

Predviden drobni material:

- 4 × M3 približno 12 mm za sprednji okvir v slepe 2,7-mm pilotske izvrtine.
- 4 × M3 približno 8 mm za notranji pladenj v enake pilotske izvrtine.
- 4 × M3 z maticami za zaslon; dolžina je odvisna od izbranih distančnikov.
- 4 × M3 približno 12 mm z maticami za GPS in 2 × M3 s podložkami/maticami za IMU.
- 2,5-mm vezice za ESP in razbremenitev zunanjih kablov.
- Kovinski vijak, matica in podložke za M6 pregib.

Pilotske izvrtine niso kovinski navoji: za običajne strojne vijake M3 jih ustrezno
pripravi/navojno obdela in ne privijaj na silo. Ležišča za toplotne vložke niso dodana.
Matice pod pladnjem imajo 3 mm prostora nad zadnjo ploščo.

Za prvi PETG poskus uporabi profil proizvajalca filamenta, plast približno 0,2 mm,
5–6 obodov in približno 35–45 % polnila; to so začetne nastavitve, ne potrjena
mehanska specifikacija. Pladenj tiskaš z ravno spodnjo stranjo na mizi. Ohišje ima
ravno zadnjo stran; pod spodnjim pregibom so pri tej orientaciji potrebne podpore.
Pri okvirju preveri podpore za poličko antene in možnost odstranitve skozi odprto stran.
Noben STL nima vgrajenih podpor ali orientacije, ki bi jo moral slicer obvezno uporabiti.

Prototip ima velike odprtine za servis in ni vodotesen. Pred uporabo na motorju
preveri prileganje, vpetje, gibanje krmila ter obnašanje pregiba pod dejansko obremenitvijo.

## Ponovljivost in preverjanje

- `parameters.json`: vse vhodne mere, tudi začasne.
- `geometry.py`: skupna geometrija in osnovni prostorski pogoji.
- `LapTimerPrototype.py`: nativni model za Fusion.
- `build_blender.py`: preverjen generator STL in predogledov brez Fusiona.
- `build_cad.py`: generator polnih STEP teles prek CadQuery.
- `exports/mesh_validation.json`: sklenjenost mrež, število povezanih delov,
  mere, prostornine in preverjanje trkov pregiba.
- `exports/validation.json`, če je prisoten: dodatno preverjanje STEP teles in STL.
- `exports/dimensions.svg`: kotirana skica ohišja in mere vmesnika za podstavek.

Skripti za Blender in CadQuery znova ustvarita samo datoteke v lokalni mapi `exports`.
Za spremembe po parametrih uporabi Fusion ali popravi JSON in ponovno generiraj.

Uporabljena dokumentacija:
[Fusion skripte in API](https://help.autodesk.com/view/fusion360/ENU/?guid=GUID-D93DF10F-4209-4073-A2A0-4FA8788C8709),
[uporabniški parametri](https://help.autodesk.com/cloudhelp/ENU/Fusion-360-API/files/fusion_UserParameters_add.htm),
[izvleki](https://help.autodesk.com/cloudhelp/ENU/Fusion-360-API/files/ExtrudeFeatureSample_Sample.htm).
