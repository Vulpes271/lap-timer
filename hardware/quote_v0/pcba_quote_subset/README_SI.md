# DELNI PCBA PREDRAČUN — NE ZA NAROČILO

Za poskus uvoza na JLCPCB uporabi Gerber ZIP iz nadrejene mape in:
- BOM_PARTIAL_QUOTE_ONLY.csv kot BOM;
- CPL_PARTIAL_QUOTE_ONLY.csv kot Pick & Place / CPL.

Datoteki vključujeta SAMO U1 (ESP32-S3-WROOM-1-N8R8) in U2 (ICM-42688-P).
Ne vključujeta ostalih 32 odtisov, napajalnika, zaslona, GNSS ali CC1101.
Prikazana cena zato NI cena sestavljene funkcionalne naprave.

Šifri delov sta preverjeni na LCSC 2026-10-27; razpoložljivost za JLCPCB
mora potrditi njihov uvoz. Footprint U2 je še kandidat: pinout in rotacija
nista potrjena. Pred izdelavo preveri oba odtisa proti datasheetom in
orientacijo v pregledovalniku sestave. Plošča nima povezav in ne bo delovala.

CPL je filtriran iz priloženega izvirnega KiCad izvoza. Koordinate uporabljajo
isti izhodiščni sistem kot Gerberji (zato je Y negativen); ne premikaj jih
samodejno na spodnji levi kot. Ujemanje položajev preveri v JLC pregledovalniku.
Celotni izvoz kicad_positions_ALL_UNAPPROVED.csv je sledljivostni vir, ne CPL
za sestavo vseh elementov.

Viri identifikacije delov:
https://www.lcsc.com/product-detail/C2913201.html
https://www.lcsc.com/product-detail/C1850418.html
