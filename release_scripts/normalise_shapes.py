"""Rewrite the library shapes into a canonical, recolourable form.

Each shape becomes a single <g id="ub-body"> carrying the fill and stroke, with
its geometry inside and no per-element colour, so recolouring is a matter of
setting two attributes on one element and letting SVG inheritance do the rest.
"""
import os, re, sys
import xml.dom.minidom as minidom

DRAW = {'path', 'polygon', 'polyline', 'rect', 'circle', 'ellipse', 'line'}
# Colour-ish presentation attributes that must not survive on the geometry,
# or they would override the group's inherited values.
STRIP = {'fill', 'stroke', 'style', 'stroke-width', 'stroke-opacity', 'fill-opacity',
         'opacity', 'stroke-linecap', 'stroke-linejoin', 'stroke-miterlimit',
         'fill-rule', 'stroke-dasharray', 'class'}

DEFAULT_STROKE = '#595959'


def local(tag):
    return tag.split(':')[-1]


def is_degenerate(el):
    """A path whose data never draws: a lone moveto, or empty."""
    if local(el.tagName) != 'path':
        return False
    d = el.getAttribute('d').strip()
    if not d:
        return True
    # Anything beyond the initial moveto counts as drawing.
    return re.fullmatch(r'[Mm][\s,0-9.eE+-]*', d) is not None


def is_artboard(el, vb):
    """Illustrator leaves a full-canvas rect behind; it is not part of the shape."""
    if local(el.tagName) != 'rect':
        return False
    try:
        x = float(el.getAttribute('x') or 0)
        y = float(el.getAttribute('y') or 0)
        w = float(el.getAttribute('width'))
        h = float(el.getAttribute('height'))
    except ValueError:
        return False
    parts = vb.split()
    if len(parts) != 4:
        return False
    vx, vy, vw, vh = (float(p) for p in parts)
    near = lambda a, b: abs(a - b) < 0.01
    return near(x, vx) and near(y, vy) and near(w, vw) and near(h, vh)


def paints_fill(el):
    """True when the element draws itself as a filled area rather than an outline."""
    fill = el.getAttribute('fill').strip().lower()
    style = el.getAttribute('style').lower()
    match = re.search(r'fill\s*:\s*([^;]+)', style)
    if match:
        fill = match.group(1).strip()
    return bool(fill) and fill not in ('none', 'transparent')


def subpaths(d):
    """Split path data on its moveto commands."""
    parts = re.split(r'(?=[Mm])', d)
    return [p for p in (q.strip() for q in parts) if p]


def extent(d):
    nums = [float(n) for n in re.findall(r'-?\d*\.?\d+(?:[eE][-+]?\d+)?', d)]
    xs, ys = nums[0::2], nums[1::2]
    if not xs or not ys:
        return 0.0
    return (max(xs) - min(xs)) * (max(ys) - min(ys))


def outline_of(el):
    """For a filled ring, the outer contour, so it can be drawn as an outline.

    Three of these shapes draw their border as a filled compound path with an
    outer and an inner contour. Stroking that as-is would draw both.
    """
    if local(el.tagName) != 'path' or not paints_fill(el):
        return None
    parts = subpaths(el.getAttribute('d'))
    if len(parts) < 2:
        return None
    return max(parts, key=extent)


def geometry_key(el):
    """Identity for duplicate detection: tag plus the geometry attributes."""
    keep = {k: v for k, v in el.attributes.items() if k not in STRIP and k != 'id'}
    return (local(el.tagName), tuple(sorted(keep.items())))


# Containers worth descending into. Illustrator wraps its real geometry in a
# <switch> next to a <foreignObject> holding its own private data.
CONTAINERS = {'g', 'svg', 'switch', 'a'}


def collect(node, inherited, out):
    for child in node.childNodes:
        if child.nodeType != child.ELEMENT_NODE:
            continue
        tag = local(child.tagName)
        transform = (inherited + ' ' + child.getAttribute('transform')).strip()
        if tag in DRAW:
            out.append((child, transform))
        elif tag in CONTAINERS:
            collect(child, transform, out)
        # defs, metadata, namedview, foreignObject and friends are dropped


def normalise(path):
    doc = minidom.parse(path)
    svg = doc.documentElement

    width = svg.getAttribute('width').replace('px', '').strip()
    height = svg.getAttribute('height').replace('px', '').strip()
    viewbox = svg.getAttribute('viewBox').strip() or '0 0 %s %s' % (width, height)
    parts = viewbox.split()
    if len(parts) == 4:
        width, height = parts[2], parts[3]

    found = []
    collect(svg, '', found)

    elements, seen = [], set()
    for el, transform in found:
        if is_degenerate(el) or is_artboard(el, viewbox):
            continue
        key = geometry_key(el)
        if key in seen:
            continue
        seen.add(key)
        elements.append((el, transform))

    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="%s">'
             % (width, height, viewbox),
             '  <g id="ub-body" fill="none" stroke="%s" stroke-width="1">' % DEFAULT_STROKE]

    for el, transform in elements:
        # Prefixed attributes (inkscape:*, i:*, sodipodi:*) would be unbound here,
        # since the canonical form declares no namespaces but the SVG one.
        attrs = {k: v for k, v in el.attributes.items()
                 if k not in STRIP and k != 'transform' and ':' not in k}
        attrs.pop('id', None)
        if transform:
            attrs['transform'] = transform
        outline = outline_of(el)
        if outline is not None:
            attrs['d'] = outline

        rendered = ' '.join('%s="%s"' % (k, attrs[k]) for k in sorted(attrs))
        lines.append('    <%s %s/>' % (local(el.tagName), rendered))

    lines.append('  </g>')
    lines.append('</svg>')
    return '\n'.join(lines) + '\n', len(elements)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if not name.startswith('nofill_') or not name.endswith('.svg'):
            continue
        text, count = normalise(os.path.join(src, name))
        if count == 0:
            raise SystemExit('ERROR: %s produced no geometry' % name)
        out = name[len('nofill_'):]
        with open(os.path.join(dst, out), 'w') as fh:
            fh.write(text)
        print('%-26s -> %-22s %d elems, %d bytes' % (name, out, count, len(text)))
