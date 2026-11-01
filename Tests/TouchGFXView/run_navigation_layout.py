"""Check actual user-View geometry emitted by native tests; optional approximate font preview."""
from pathlib import Path
import argparse
import json
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--preview', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
layout = json.loads((root/'Logs/TouchGFX/U3/tests/view/navigation-layout.json').read_text())
keys = re.findall(r'^    (T_\w+),?$', (root/'TouchGFX/generated/texts/include/texts/TextKeysAndLanguages.hpp').read_text(), re.M)
database = ET.parse(root/'TouchGFX/assets/texts/texts.xml').getroot()
texts = {t.attrib['Id']: (int(t.attrib['TypographyId'][4:]), t.find('Translation').text)
         for t in database.findall('./Texts/TextGroup/Text')}
fonts = {}
for size in {size for size, _ in texts.values()}:
    data = (root/f'TouchGFX/generated/fonts/src/Table_verdana_{size}_4bpp.cpp').read_text()
    fonts[size] = {int(code, 16): int(advance) for code, advance in re.findall(
        r'\{\s*\d+,\s*0x([0-9A-Fa-f]+),\s*\d+,\s*\d+,\s*\d+,\s*-?\d+,\s*(\d+),', data)}
for widget in layout:
    assert 0 <= widget['x'] and 0 <= widget['y'], widget
    assert widget['x'] + widget['w'] <= 480 and widget['y'] + widget['h'] <= 272, widget
    if widget['typed'] < 0:
        continue
    size, static_text = texts[keys[widget['typed']][2:]]
    widget['font_size'] = size
    widget['label'] = static_text if widget['text'] is None else widget['text']
    assert '<>' not in widget['label'], widget
    assert all(ord(c) in fonts[size] for c in widget['label']), widget
    assert sum(fonts[size][ord(c)] for c in widget['label']) <= widget['w'], widget
    assert size <= widget['h'], widget
    if widget['name'] != 'navLabel':
        assert widget['y'] + widget['h'] <= 228, widget
pages=sorted({w['page'] for w in layout})
for page in pages:
    page_widgets = [w for w in layout if w['page'] == page]
    assert [w['label'] for w in page_widgets if w['name'] == 'navLabel'] == ['RANGE', 'CLAP', 'WAVE', 'SETUP']
    assert sum(w['name'] == 'measurePanel' for w in page_widgets) == (page == 0)
    assert sum(w['name'] == 'settingTile' for w in page_widgets) == (3 if page >= 3 else 0)
    plots = [w for w in page_widgets if w['name'] == 'waveform']
    assert len(plots) == (1 if page == 2 else 0), page
    if plots:
        plot = plots[0]
        assert plot['y'] + plot['h'] <= 228, plot
        assert all(w['y'] + w['h'] <= plot['y'] for w in page_widgets
                   if w['name'] in ('pageTitle', 'pageLine')), page
print('PASS navigation/calibration layout: pages and confirmation states, bounds, actual glyph widths, no text/nav overlap')

if args.preview:
    from PIL import Image, ImageDraw, ImageFont
    image = Image.new('RGB', (980, ((len(pages)+1)//2)*282), '#030812')
    for page in pages:
        origin = (10 + (page % 2) * 490, 5 + (page // 2) * 282)
        draw = ImageDraw.Draw(image)
        for widget in (w for w in layout if w['page'] == page):
            x, y = origin[0] + widget['x'], origin[1] + widget['y']
            color = '#%06x' % widget['color']
            if widget['typed'] < 0:
                draw.rectangle((x, y, x+widget['w']-1, y+widget['h']-1), fill=color)
            else:
                # Preview only: device glyph advance checks above use actual generated fonts.
                font = ImageFont.truetype('C:/Windows/Fonts/verdana.ttf', widget['font_size'])
                draw.text((x, y), widget['label'], font=font, fill=color)
    args.preview.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.preview)
    print('Approximate offline preview:', args.preview)
