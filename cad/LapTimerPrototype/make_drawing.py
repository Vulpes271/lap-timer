"""Dimensioned SVG reference for the separate motorcycle base; all labels mm."""
from pathlib import Path
import html
from geometry import load_parameters

p=load_parameters(); out=Path(__file__).with_name('exports')/'dimensions.svg'
s=3.0; x0=65; y0=390
items=['<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="650" viewBox="0 0 1100 650">',
       '<rect width="1100" height="650" fill="#f8fafc"/>',
       '<style>text{font-family:Arial,sans-serif;fill:#172b3a} .small{font-size:14px}.title{font-size:24px;font-weight:bold}.dim{stroke:#34495e;stroke-width:1;fill:none}.outline{stroke:#173c50;stroke-width:2;fill:none}</style>',
       '<text class="title" x="55" y="38">Lap timer — PETG prototype v1</text>',
       '<text class="small" x="55" y="62">All dimensions mm. Custom M6 tilt interface; not GoPro compatible.</text>']
def rect(x,y,w,h,fill='#d9e9ef',stroke='#173c50',dash=''):
    items.append(f'<rect x="{x0+x*s}" y="{y0-(y+h)*s}" width="{w*s}" height="{h*s}" fill="{fill}" stroke="{stroke}" stroke-width="1.5" stroke-dasharray="{dash}"/>')
def line(x1,y1,x2,y2): items.append(f'<line class="dim" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>')
def text(x,y,t,size=16): items.append(f'<text x="{x}" y="{y}" font-size="{size}">{html.escape(t)}</text>')
def dimh(xa,xb,y,label):
    line(xa,y,xb,y)
    for x in (xa,xb): line(x,y-6,x,y+6)
    text((xa+xb)/2-20,y-9,label)
rect(0,0,p['case_w'],p['case_h'])
rect((p['case_w']-p['screen_w'])/2,p['screen_y'],p['screen_w'],p['screen_h'],'none','#6f8390','5 4')
rect((p['case_w']-p['window_w'])/2,p['screen_y']+(p['screen_h']-p['window_h'])/2,p['window_w'],p['window_h'],'#338099')
rect((p['case_w']-p['antenna_w'])/2,p['antenna_y'],p['antenna_w'],p['antenna_h'],'#f0dfb9')
for x in ((p['case_w']-p['screen_pitch_x'])/2,(p['case_w']+p['screen_pitch_x'])/2):
    for y in (p['screen_y']+(p['screen_h']-p['screen_pitch_y'])/2,p['screen_y']+(p['screen_h']+p['screen_pitch_y'])/2):
        items.append(f'<circle cx="{x0+x*s}" cy="{y0-y*s}" r="5.1" fill="white" stroke="#173c50"/>')
for x in ((p['case_w']-2*p['fork_ear']-p['fork_gap'])/2,(p['case_w']+p['fork_gap'])/2):
    rect(x,p['pivot_y']-p['pivot_d']/2,p['fork_ear'],-p['pivot_y']+p['pivot_d']/2,'#6c8998')
axis_y=y0-p['pivot_y']*s
line(x0+37*s,axis_y,x0+83*s,axis_y)
text(x0+10,y0+127,'Front view: X left/right, Y bottom/top',14)
dimh(x0,x0+p['case_w']*s,95,'120')
dimh(x0+(p['case_w']-p['screen_pitch_x'])/2*s,x0+(p['case_w']+p['screen_pitch_x'])/2*s,530,'93 hole pitch')
text(475,120,'Housing: 120 × 108 × 55',20)
text(475,150,'Overall height including hinge: 139',17)
text(475,185,'Display: 98.3 × 56; holes 93 × 50',17)
text(475,212,f"Window {p['window_w']} × {p['window_h']} — centered assumed",17)
text(475,239,'Antenna pocket: 35 × 35 × 6.7 envelope',17)
text(475,287,'Interface for your separate motorcycle base',20)
text(475,321,'M6 bolt axis runs left–right (X).',17)
text(475,349,'Ear 8 + gap 14.6 + ear 8 = 30.6 total',17)
text(475,377,'Your tongue: 14 thick, round end Ø26, bore Ø6.4',17)
text(475,405,'Axis: 18 below housing; 26 from rear face',17)
text(475,433,'M6 nut pocket: 10.3 across flats × 5 deep',17)
text(475,484,'Print the small tongue gauge before the whole housing.',17)
text(475,516,'The motorcycle base and its attachment are not included.',16)
text(55,608,'Nominal dimensions; no structural or vibration validation. See README before printing.',15)
items.append('</svg>'); out.write_text('\n'.join(items),encoding='utf-8')
