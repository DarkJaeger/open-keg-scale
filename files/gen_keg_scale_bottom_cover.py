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
         - Stem  : 20 mm dia x 14 mm  (snug in 21.5 mm body holes)
         - Pocket: 15 mm x 8.5 mm x 3 mm deep centred on stem top
           (GML692 boss is 14 x 7.5 x 2.5 mm, 0.5 mm clearance each side)
           Foot is glued to load cell boss

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
STEM_H         =  14.0   # mm stem height

# Pocket in stem top seats over GML692 boss (14 x 7.5 x 2.5 mm) with 0.5 mm clearance
POCKET_L       =  15.0   # mm (boss 14 mm + 0.5 mm each side)
POCKET_W       =   8.5   # mm (boss 7.5 mm + 0.5 mm each side)
POCKET_D       =   3.0   # mm depth (boss protrudes 2.5 mm)

FOOT_CENTERS = [
    ( 43.0,  43.0),
    (167.0,  43.0),
    ( 43.0, 167.0),
    (167.0, 167.0),
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
    Simple foot centred at (cx, cy) — stem only, no cap.
    Stem from z_bottom up to z_bottom+STEM_H.
    Pocket in stem top seats over GML692 boss; foot glued in place.
    """
    stem = trimesh.creation.cylinder(radius=STEM_R, height=STEM_H, sections=SECTIONS)
    stem = translate(stem, cx, cy, z_bottom + STEM_H / 2)

    # Pocket centred on stem top
    stem_top_z = z_bottom + STEM_H
    pocket = trimesh.creation.box(extents=[POCKET_L, POCKET_W, POCKET_D + 0.1])
    pocket = translate(pocket, cx, cy, stem_top_z - POCKET_D / 2)

    return trimesh.boolean.difference([stem, pocket], engine="manifold")


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

# Lay out 4 feet in a 2x2 grid (printed with pocket facing up)
# Space by stem diameter + 5 mm gap
spacing = STEM_R * 2 + 5.0
layout = [
    (0,       0      ),
    (spacing, 0      ),
    (0,       spacing),
    (spacing, spacing),
]

all_feet = []
for lx, ly in layout:
    foot = make_foot(lx + STEM_R, ly + STEM_R, 0)
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
Stem pocket : {POCKET_L:.1f} mm x {POCKET_W:.1f} mm x {POCKET_D:.0f} mm deep (GML692 boss 14x7.5x2.5 mm, 0.5 mm clearance)
""")
