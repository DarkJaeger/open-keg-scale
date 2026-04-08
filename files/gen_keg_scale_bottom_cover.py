"""
Keg Scale Bottom Cover Generator
=================================
Outputs two separate STL files:

  1. keg_scale_bottom_cover.stl
       Rounded-rectangle plate (32 mm corner radius, matches scale body)
       210 x 210 x 5 mm with 4 x 16.5 mm holes for feet to pass through
       Rubber-foot recesses on underside

  2. keg_scale_feet.stl
       4 cylindrical feet arranged in a 2x2 grid, ready to print flat
       Each foot: 16 mm dia, 15 mm tall + 1 mm dome tip
       Feet pass up through the cover plate holes and into the scale body
       to contact the load cell free ends

Scale body: 210 x 210 x 15 mm, corner holes at (32,32),(178,32),(32,178),(178,178)
"""

import trimesh
import numpy as np
from shapely.geometry import Point
from shapely.ops import unary_union

# ── Parameters ────────────────────────────────────────────────────────────────
SCALE_W       = 210.0
SCALE_D       = 210.0
CORNER_R      =  32.0   # matches scale body

PLATE_H       =   5.0   # mm, cover plate thickness
FOOT_HOLE_R   =   8.25  # mm, hole radius in plate (0.25 mm clearance on foot)

FOOT_R        =   8.0   # mm radius = 16 mm dia
FOOT_H        =  15.0   # mm tall (below cover + up into housing)
DOME_H        =   1.0   # mm dome on tip

FOOT_CENTERS = [
    ( 32.0,  32.0),
    (178.0,  32.0),
    ( 32.0, 178.0),
    (178.0, 178.0),
]

RUBBER_R      =   8.0
RUBBER_H      =   2.0
RUBBER_OFFSET =  15.0

SECTIONS = 64

# ── Helpers ───────────────────────────────────────────────────────────────────
def translate(mesh, tx, ty, tz):
    mesh.apply_translation([tx, ty, tz])
    return mesh


def rounded_rect_extrusion(w, d, r, h, sections=SECTIONS):
    corners = [
        Point(r,     r    ),
        Point(w - r, r    ),
        Point(r,     d - r),
        Point(w - r, d - r),
    ]
    poly = unary_union([c.buffer(r, resolution=sections // 4) for c in corners]).convex_hull
    return trimesh.creation.extrude_polygon(poly, h)


def dome_cap(cx, cy, z_base, radius, dome_h, sections=SECTIONS):
    angles = np.linspace(0, 2 * np.pi, sections, endpoint=False)
    rim = np.column_stack([
        cx + radius * np.cos(angles),
        cy + radius * np.sin(angles),
        np.full(sections, z_base),
    ])
    tip = np.array([[cx, cy, z_base + dome_h]])
    verts = np.vstack([rim, tip])
    faces = np.array([[i, (i + 1) % sections, sections] for i in range(sections)], dtype=np.int32)
    return trimesh.Trimesh(vertices=verts, faces=faces, process=False)


# ════════════════════════════════════════════════════════════════════════════════
# FILE 1 — Bottom cover plate
# ════════════════════════════════════════════════════════════════════════════════
print("Building cover plate ...")

plate = rounded_rect_extrusion(SCALE_W, SCALE_D, CORNER_R, PLATE_H, SECTIONS)

# Holes for feet
cuts = []
for (fx, fy) in FOOT_CENTERS:
    hole = trimesh.creation.cylinder(radius=FOOT_HOLE_R, height=PLATE_H + 1.0, sections=SECTIONS)
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
cover = trimesh.boolean.difference([plate, all_cuts], engine="manifold")

print(f"  Watertight: {cover.is_watertight}  Triangles: {len(cover.faces)}")
cover_path = "C:/Users/rig2/Downloads/keg_scale_bottom_cover.stl"
cover.export(cover_path)
print(f"  Saved to {cover_path}")


# ════════════════════════════════════════════════════════════════════════════════
# FILE 2 — Feet (4 individual cylinders laid out for printing)
# ════════════════════════════════════════════════════════════════════════════════
print("\nBuilding feet ...")

# Lay out 4 feet in a 2x2 grid with 5 mm spacing, all at Z=0 (print flat)
# Each foot: cylinder + dome cap
SPACING = FOOT_R * 2 + 5.0   # gap between feet when printing
layout = [
    (0,       0      ),
    (SPACING, 0      ),
    (0,       SPACING),
    (SPACING, SPACING),
]

foot_parts = []
for (lx, ly) in layout:
    cx = lx + FOOT_R
    cy = ly + FOOT_R
    cyl = trimesh.creation.cylinder(radius=FOOT_R, height=FOOT_H, sections=SECTIONS)
    cyl = translate(cyl, cx, cy, FOOT_H / 2)
    foot_parts.append(cyl)

    dome = dome_cap(cx, cy, FOOT_H, FOOT_R, DOME_H)
    foot_parts.append(dome)

watertight_feet = [m for m in foot_parts if m.is_watertight]
feet = trimesh.boolean.union(watertight_feet, engine="manifold")

print(f"  Watertight: {feet.is_watertight}  Triangles: {len(feet.faces)}")
feet_path = "C:/Users/rig2/Downloads/keg_scale_feet.stl"
feet.export(feet_path)
print(f"  Saved to {feet_path}")

print("\nDone.")
print(f"  Cover: 210 x 210 x {PLATE_H:.0f} mm, 32 mm corner radius, {FOOT_HOLE_R*2:.1f} mm foot holes")
print(f"  Feet:  {FOOT_R*2:.0f} mm dia x {FOOT_H:.0f} mm tall + {DOME_H:.0f} mm dome, 4 off")
