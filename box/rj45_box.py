"""RJ45 2x4 adapter box — tilted (sawtooth) front so jacks face slightly downward.
Wall-mounted with four keyhole slots in the back plate: put four small countersunk
screws in the wall, then slide the box down onto them.

Axes: X = width, Y = depth (front at Y=0, wall side at Y=D), Z = height.
All dimensions in mm. Change parameters at the top and re-run:  python rj45_box.py <outdir>
"""
import math
import cadquery as cq

# ---- parameters ----
W, H, D = 104.0, 52.0, 30.0     # outer box, total depth including the back plate
WALL = 2.0                      # wall / back plate thickness
R = 2.0                         # edge rounding (front and corners; wall side stays sharp)
SIDE = 20.0                     # side strips that stay flat (hole starts 20 mm in)
HOLE_W, HOLE_H = 64.0, 14.0     # RJ45 cut-out (HOLE_H measured along the tilted face)
TILT = 15.0                     # degrees the jack faces point downward
# each facet: (z_top, z_bottom, opening centre height)
FACETS = [(48.0, 26.0, 35.0),   # top row: hole top edge ≈ 10 mm below box top
          (26.0, 4.0, 15.0)]    # bottom row
CLEAR = 0.2                     # back plate fit clearance

# keyholes (wall screws: small countersunk, e.g. 3–3.5 mm wood screw, head ≤ 7.5 mm)
KEY_X = (15.0, W - 15.0)        # keyhole centre lines
KEY_Z = (9.0, 35.0)             # centre of the head hole (bottom end); slot runs upward
KEY_HEAD = 8.0                  # head entry hole
KEY_SLOT = 4.0                  # slot width (screw shank)
KEY_LEN = 8.0                   # how far the box slides down
POCKET_D = 3.0                  # room behind the plate for the screw head
BOSS_T = 1.5                    # cover over the pocket, keeps the head away from the PCB

# back plate retaining screws (M2.5 or 2.2 mm self-tapping into body pillars)
LS_Z = 26.0
LS_HOLE, LS_CSK, LS_PILOT = 2.8, 5.5, 2.2

DB = D - WALL                   # body depth (back plate sits on top)
t = math.tan(math.radians(TILT))

# ---- outer solid: round every edge except those on the wall side ----
outer = (cq.Workplane("XY").box(W, DB, H, centered=False)
         .edges("not >Y").fillet(R))

for zt, zb, _ in FACETS:
    d = (zt - zb) * t
    notch = (cq.Workplane("YZ", origin=(SIDE, 0, 0))
             .polyline([(0, zt), (d, zb), (0, zb)]).close()
             .extrude(W - 2 * SIDE))
    outer = outer.cut(notch)

# round the sawtooth edges (tooth tips and ledges) one at a time
def _notch_x_edges(wp):
    sel = []
    for e in wp.edges().vals():
        bb = e.BoundingBox()
        if (e.geomType() == "LINE" and bb.xlen > W - 2 * SIDE - 1
                and bb.xmax < W - SIDE + 0.5 and bb.ymax < DB / 2):
            sel.append(e)
    return sel

_done = set()
while True:
    key = lambda e: (round(e.BoundingBox().ymin, 1), round(e.BoundingBox().zmin, 1))
    todo = [e for e in _notch_x_edges(outer) if key(e) not in _done]
    if not todo:
        break
    e = todo[0]
    _done.add(key(e))
    for r in (R, 1.5, 1.0):          # the edge next to the bottom rounding only fits ~1.5
        try:
            outer = outer.newObject([outer.val().fillet(r, [e])])
            break
        except Exception:
            pass

# ---- hollow it out, open at the back ----
cavity = (cq.Workplane("XY")
          .box(W - 2 * WALL, DB, H - 2 * WALL, centered=False)
          .translate((WALL, WALL, WALL)))
for zt, zb, _ in FACETS:                      # keep a WALL-thick skin behind each tooth
    d = (zt - zb) * t
    keep = (cq.Workplane("YZ", origin=(SIDE - WALL, 0, 0))
            .polyline([(-5, zt), (0, zt), (d, zb), (-5, zb)]).close()
            .offset2D(WALL, kind="intersection")
            .extrude(W - 2 * SIDE + 2 * WALL))
    cavity = cavity.cut(keep)
body = outer.cut(cavity)

# ---- RJ45 openings, cut perpendicular to each tilted facet ----
for zt, zb, zc in FACETS:
    yc = (zt - zc) * t
    cutter = (cq.Workplane("XY")
              .box(HOLE_W, 6.0, HOLE_H)
              .rotate((0, 0, 0), (1, 0, 0), TILT)
              .translate((W / 2, yc + 1.0, zc)))
    body = body.cut(cutter)

# ---- pillars on the side walls for the back plate screws ----
PILLAR_W, PILLAR_H, PILLAR_D = 6.0, 6.0, 10.0
for x0, xh in ((WALL, WALL + PILLAR_W / 2), (W - WALL - PILLAR_W, W - WALL - PILLAR_W / 2)):
    pillar = (cq.Workplane("XY").box(PILLAR_W, PILLAR_D, PILLAR_H, centered=False)
              .translate((x0, DB - PILLAR_D, LS_Z - PILLAR_H / 2)))
    body = body.union(pillar)
    body = body.cut(cq.Workplane("XZ", origin=(0, DB, 0)).center(xh, LS_Z)
                    .circle(LS_PILOT / 2).extrude(PILLAR_D - 1.0))
LS_X = (WALL + PILLAR_W / 2, W - WALL - PILLAR_W / 2)

# ---- back plate (in assembly position, Y = DB..D) ----
plate = (cq.Workplane("XY").box(W, WALL, H, centered=False)
         .translate((0, DB, 0)).edges("|Y").fillet(R))

LIP = 3.0
lip = (cq.Workplane("XY")
       .box(W - 2 * WALL - 2 * CLEAR, LIP, H - 2 * WALL - 2 * CLEAR, centered=False)
       .translate((WALL + CLEAR, DB - LIP, WALL + CLEAR)))
lip = lip.cut(cq.Workplane("XY")
              .box(W - 4 * WALL - 2 * CLEAR, LIP, H - 4 * WALL - 2 * CLEAR, centered=False)
              .translate((2 * WALL + CLEAR, DB - LIP, 2 * WALL + CLEAR)))
# notch the lip where the pillars are
for xh in LS_X:
    lip = lip.cut(cq.Workplane("XY").box(PILLAR_W + 2 * CLEAR + 4, LIP, PILLAR_H + 2 * CLEAR,
                                         centered=(True, False, True))
                  .translate((xh, DB - LIP, LS_Z)))
lid = plate.union(lip)


def _stadium(wp, w, length):
    """slot of width w from z=0 up to z=length (centre to centre), on an XZ workplane"""
    return wp.center(0, length / 2).slot2D(length + w, w, angle=90)


BOSS_DEPTH = POCKET_D + BOSS_T
for kx in KEY_X:
    for kz in KEY_Z:
        # boss on the inside of the plate, holding the head pocket
        boss = _stadium(cq.Workplane("XZ", origin=(0, DB, 0)).center(kx, kz),
                        KEY_HEAD + 5.0, KEY_LEN).extrude(BOSS_DEPTH)
        lid = lid.union(boss)
        # pocket for the head to slide in, behind the plate
        pocket = _stadium(cq.Workplane("XZ", origin=(0, DB, 0)).center(kx, kz),
                          KEY_HEAD + 0.6, KEY_LEN).extrude(POCKET_D)
        lid = lid.cut(pocket)
        # through the plate: head hole + slot
        lid = lid.cut(cq.Workplane("XZ", origin=(0, D, 0)).center(kx, kz)
                      .circle(KEY_HEAD / 2).extrude(WALL))
        lid = lid.cut(_stadium(cq.Workplane("XZ", origin=(0, D, 0)).center(kx, kz),
                               KEY_SLOT, KEY_LEN).extrude(WALL))

# countersunk holes for the two plate-to-body screws (countersink on the wall side)
for xh in LS_X:
    lid = lid.cut(cq.Workplane("XZ", origin=(0, D, 0)).center(xh, LS_Z)
                  .circle(LS_HOLE / 2).extrude(WALL))
    lid = lid.cut(cq.Solid.makeCone(LS_CSK / 2, 0, LS_CSK / 2,
                                    cq.Vector(xh, D, LS_Z), cq.Vector(0, -1, 0)))

if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    # print orientation: lid flat with the wall side on the bed
    lid_print = lid.translate((0, -DB, 0))
    cq.exporters.export(body, f"{out}/rj45_box_body.step")
    cq.exporters.export(body, f"{out}/rj45_box_body.stl", tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(lid_print, f"{out}/rj45_box_lid.step")
    cq.exporters.export(lid_print, f"{out}/rj45_box_lid.stl", tolerance=0.02, angularTolerance=0.1)
    asm = cq.Assembly().add(body, name="body").add(lid, name="back_plate")
    asm.save(f"{out}/rj45_box_assembly.step")
    print("ok", body.val().isValid(), lid.val().isValid())
