"""Check generated font coverage/advance widths and screen bounds, without a simulator."""
from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[2]
app=json.loads((root/'TouchGFX/SBCG.touchgfx').read_text())['Application']
db=ET.parse(root/'TouchGFX/assets/texts/texts.xml').getroot()
texts={t.attrib['Id']:(int(t.attrib['TypographyId'][4:]),t.find('Translation').text)
       for t in db.findall('./Texts/TextGroup/Text')}
fonts={}
for size in {size for size,_ in texts.values()}:
    data=(root/f'TouchGFX/generated/fonts/src/Table_verdana_{size}_4bpp.cpp').read_text()
    fonts[size]={int(code,16):int(advance) for code,advance in re.findall(
        r'\{\s*\d+,\s*0x([0-9A-Fa-f]+),\s*\d+,\s*\d+,\s*\d+,\s*-?\d+,\s*(\d+),',data)}
stress={'eventsText':['EVENTS 999999999999+'], 'detailText':['DROP 9999+ ERR 9999+'],
        'syncText':['LOCAL REFERENCE','INTERNAL LOCK','ACQUIRING','NOT SYNCED'],
        'temperatureText':['-40.0 C','125.0 C'], 'offsetText':['-999999.999 US','OUT OF RANGE'],
        'roleText':['UNKNOWN BOARD'], 'networkText':['UNKNOWN','LINK UP','NO LINK']}
for c in app['Screens'][0]['Components']:
    assert 0<=c.get('X',0) and 0<=c.get('Y',0)
    assert c.get('X',0)+c['Width']<=480 and c.get('Y',0)+c['Height']<=272,c['Name']
    if c['Type']!='TextArea':continue
    size,text=texts[c['TextId']]
    samples=stress.get(c['Name'],[])
    if 'Wildcard1' in c:
        samples=samples+[texts[c['Wildcard1']['TextId']][1]]
    else:samples=samples+[text]
    for sample in samples:
        assert all(ord(ch) in fonts[size] for ch in sample),(c['Name'],sample)
        width=sum(fonts[size][ord(ch)] for ch in sample)
        assert width<=c['Width'],(c['Name'],sample,width,c['Width'])
print('PASS Designer layout: 28 widget bounds, static/stress glyph coverage and advance widths')
