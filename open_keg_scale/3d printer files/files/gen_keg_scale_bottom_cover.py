"""
Keg Scale Bottom Cover Generator
=================================
Outputs two separate STL files:

  1. keg_scale_bottom_cover.stl
       Rounded-rectangle base plate (32 mm corner radius, matches scale body)
       210 x 210 x 5 mm with 4 x 21.5 mm through-holes for foot stems
       Rubber-foot recesses on underside
       Side wall 3 mm thick x 8.25 mm tall around inside perimeter:
         - 5.25 mm for load cells hanging down from scale body
         - 3.00 mm for foot cap clearance
         = 8.25 mm total cavity height

  2. keg_scale_feet.stl
       4 T-shaped feet laid out flat for printing
       Each foot:
         - Stem  : 20 mm dia x 18 mm  (snug in 21.5 mm body holes)
         - Cap   : 36 mm dia x  3 mm  (sits inside body above hole)

  Assembly:
    1. Drop each foot in from the top of the scale body, cap down
    2. Stem passes down through the body hole and through the cover plate hole
    3. Cover plate slides on from below, closing the bottom
    4. Load cell free ends rest on caps and are located by the 2 pins (28 mm C-C)

  Scale body: 210 x 210 x 15 mm, outer corner radius = 12 mm
    Foot hole centres: 51 mm from each corner edge = (51,51),(159,51),(51,159),(159,159)
"""

import trimesh
import numpy as np
from shapely.geometry import Point
from shapely.ops import unary_union

# ── Parameters ────────────────────────────────────────────────────────────────
SCALE_W        = 210.0
SCALE_D        = 210.0
CORNER_R       =  12.0   # matches scale body corner radius (verified from STL arc fit)

PLATE_H        =   5.0   # mm, cover plate thickness
WALL_T         =   3.0   # mm, side wall thickness
WALL_H         =   8.25  # mm, side wall height above plate top
                         #   = 5.25 mm load cell hang-down + 3.00 mm cap clearance
COVER_HOLE_R   =  10.75  # mm, hole in cover plate (= body hole radius, 21.5 mm dia)

# Foot geometry
STEM_R         =  10.0   # mm radius = 20 mm dia  (snug in 21.5 mm body holes)
STEM_H         =  18.0   # mm stem height

CAP_R          =  18.0   # mm radius = 36 mm dia (wider than hole, retained inside body)
CAP_H          =   3.0   # mm cap thickness

FOOT_CENTERS = [
    ( 40.0,  40.0),
    (170.0,  40.0),
    ( 40.0, 170.0),
    (170.0, 170.0),
]

RUBBER_R       =   8.0
RUBBER_H       =   2.0
RUBBER_OFFSET  =  15.0

SECTIONS = 64

# ── Helpers ───────────────────────────────────────────────────────────────────
def translate(mesh, tx, ty, tz):
    mesh.apply_translation([tx, ty, tz])
    return mesh


def rounded_rect_extrusion(w, d, r, h, sections=SECTIONS):
    from shapely.geometry import box as shbox
    corner_pts = [
        Point(r,     r    ),
        Point(w - r, r    ),
        Point(r,     d - r),
        Point(w - r, d - r),
    ]
    # Union of 4 corner circles + 2 axis-aligned strips = proper rounded rectangle
    circles = [c.buffer(r, resolution=sections // 4) for c in corner_pts]
    h_strip = shbox(r, 0, w - r, d)
    v_strip = shbox(0, r, w, d - r)
    poly = unary_union(circles + [h_strip, v_strip])
    return trimesh.creation.extrude_polygon(poly, h)


def make_foot(cx, cy, z_bottom):
    """
    T-shaped foot centred at (cx, cy).
    Stem from z_bottom up to z_bottom+STEM_H.
    Cap on top of stem (36 mm dia x CAP_H).
    """
    stem = trimesh.creation.cylinder(radius=STEM_R, height=STEM_H, sections=SECTIONS)
    stem = translate(stem, cx, cy, z_bottom + STEM_H / 2)

    cap = trimesh.creation.cylinder(radius=CAP_R, height=CAP_H, sections=SECTIONS)
    cap = translate(cap, cx, cy, z_bottom + STEM_H + CAP_H / 2)

    return trimesh.boolean.union([stem, cap], engine="manifold")


# ════════════════════════════════════════════════════════════════════════════════
# FILE 1 — Bottom cover plate
# ════════════════════════════════════════════════════════════════════════════════
print("Building cover plate ...")

# Full solid: base plate height + side wall height
total_h = PLATE_H + WALL_H
solid = rounded_rect_extrusion(SCALE_W, SCALE_D, CORNER_R, total_h, SECTIONS)

# Inner cavity to hollow out the wall (leaves WALL_T thick perimeter)
# Inner rect is WALL_T inset on each side; corner radius shrinks by WALL_T
inner_w = SCALE_W - 2 * WALL_T
inner_d = SCALE_D - 2 * WALL_T
inner_r = CORNER_R - WALL_T          # concentric with outer corners
inner_cavity = rounded_rect_extrusion(inner_w, inner_d, inner_r, WALL_H + 0.1, SECTIONS)
inner_cavity = translate(inner_cavity, WALL_T, WALL_T, PLATE_H)   # sits on top of base plate

cuts = [inner_cavity]

# Foot stem holes (through base plate only)
for (fx, fy) in FOOT_CENTERS:
    hole = trimesh.creation.cylinder(radius=COVER_HOLE_R, height=PLATE_H + 1.0, sections=SECTIONS)
    hole = translate(hole, fx, fy, PLATE_H / 2)
    cuts.append(hole)

# Rubber foot recesses (underside)
for rx, ry in [
    (RUBBER_OFFSET,           RUBBER_OFFSET),
    (SCALE_W - RUBBER_OFFSET, RUBBER_OFFSET),
    (RUBBER_OFFSET,           SCALE_D - RUBBER_OFFSET),
    (SCALE_W - RUBBER_OFFSET, SCALE_D - RUBBER_OFFSET),
]:
    recess = trimesh.creation.cylinder(radius=RUBBER_R, height=RUBBER_H + 0.1, sections=32)
    recess = translate(recess, rx, ry, RUBBER_H / 2)
    cuts.append(recess)

all_cuts = trimesh.boolean.union(cuts, engine="manifold")
cover = trimesh.boolean.difference([solid, all_cuts], engine="manifold")

print(f"  Watertight : {cover.is_watertight}   Triangles : {len(cover.faces)}")
cover_path = "C:/Users/rig2/Downloads/keg_scale_bottom_cover.stl"
cover.export(cover_path)
print(f"  Saved  ->  {cover_path}")


# ════════════════════════════════════════════════════════════════════════════════
# FILE 2 — Feet (4 individual T-shaped feet laid out for printing)
# ════════════════════════════════════════════════════════════════════════════════
print("\nBuilding feet ...")

# Lay out 4 feet in a 2x2 grid (cap flat on bed, stem pointing up)
spacing = CAP_R * 2 + 5.0
layout = [
    (0,       0      ),
    (spacing, 0      ),
    (0,       spacing),
    (spacing, spacing),
]

all_feet = []
for lx, ly in layout:
    foot = make_foot(lx + CAP_R, ly + CAP_R, 0)
    all_feet.append(foot)

# Union all 4 into one file
feet_mesh = trimesh.boolean.union(all_feet, engine="manifold")

print(f"  Watertight : {feet_mesh.is_watertight}   Triangles : {len(feet_mesh.faces)}")
feet_path = "C:/Users/rig2/Downloads/keg_scale_feet.stl"
feet_mesh.export(feet_path)
print(f"  Saved  ->  {feet_path}")

print(f"""
Summary
-------
Cover plate : 210 x 210 x {PLATE_H:.0f} mm base + {WALL_H:.2f} mm wall | 32 mm corners | {COVER_HOLE_R*2:.1f} mm foot holes
Wall        : {WALL_T:.0f} mm thick x {WALL_H:.2f} mm tall (5.25 load cell + 3.00 cap clearance)
Foot stem   : {STEM_R*2:.0f} mm dia x {STEM_H:.0f} mm   (snug in {COVER_HOLE_R*2:.1f} mm holes)
Foot cap    : {CAP_R*2:.0f} mm dia x {CAP_H:.0f} mm (retained inside body)
""")
