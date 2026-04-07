"""
Keg Scale Bottom Cover Generator
Scale footprint: 210 x 210 mm  (from existing keg fill scale.stl)

Load cell specs (HX711 50kg parallel beam type):
  Body: 34 x 34 mm
  Height: ~10 mm
  Mounting hole spacing: 28 mm center-to-center (2x M4)

Design:
  - Base plate: 210 x 210 x 6 mm
  - 4 round cylindrical feet at corners (60 mm dia, 15 mm tall below plate)
  - Each foot has a 35 x 35 x 12 mm rectangular load cell pocket accessible from below
  - 2x M4 (4.5 mm) through-holes per pocket for load cell mounting screws
  - 5 mm wire access hole per foot through the base plate
  - Small chamfer/fillet on foot bases (approximated with tapered cylinder)

Coordinate system: Z=0 is bottom of plate, Z=6 is top of plate,
                   feet hang below (Z = -15 to Z = 0)
"""

import trimesh
import numpy as np

# ── Design parameters ──────────────────────────────────────────────────────────
SCALE_W       = 210.0   # mm, scale width  (X)
SCALE_D       = 210.0   # mm, scale depth  (Y)
PLATE_H       =   6.0   # mm, plate thickness

FOOT_R        =  30.0   # mm, foot radius  to 60 mm dia
FOOT_H        =  15.0   # mm, foot height below plate
FOOT_OFFSET   =  30.0   # mm, foot center distance from each edge corner

POCKET_W      =  35.5   # mm, load cell pocket width  (X) — 0.5 mm clearance
POCKET_D      =  35.5   # mm, load cell pocket depth  (Y)
POCKET_H      =  12.0   # mm, load cell pocket height (how deep)

M4_HOLE_D     =   4.5   # mm, M4 clearance hole diameter
HOLE_SPACING  =  28.0   # mm, center-to-center for mounting holes

WIRE_HOLE_D   =   5.0   # mm, wire access hole diameter through plate
SECTIONS      =  64     # polygon facets for cylinders

# ── Foot center positions ──────────────────────────────────────────────────────
foot_centers = [
    (FOOT_OFFSET,           FOOT_OFFSET),
    (SCALE_W - FOOT_OFFSET, FOOT_OFFSET),
    (FOOT_OFFSET,           SCALE_D - FOOT_OFFSET),
    (SCALE_W - FOOT_OFFSET, SCALE_D - FOOT_OFFSET),
]

# ── Helper: translate a mesh by (tx, ty, tz) ──────────────────────────────────
def translate(mesh, tx, ty, tz):
    mesh.apply_translation([tx, ty, tz])
    return mesh

# ── Build geometry ─────────────────────────────────────────────────────────────

# 1. Base plate  (0,0,0) to (210,210,6)
plate = trimesh.creation.box(extents=[SCALE_W, SCALE_D, PLATE_H])
plate = translate(plate, SCALE_W / 2, SCALE_D / 2, PLATE_H / 2)

additive_parts = [plate]
subtractive_parts = []

for (fx, fy) in foot_centers:

    # ── Cylindrical foot (hangs below plate) ──────────────────────────────
    # trimesh.creation.cylinder centers at origin along Z
    # We want it from Z = -FOOT_H to Z = 0  to center at Z = -FOOT_H/2
    foot = trimesh.creation.cylinder(
        radius=FOOT_R, height=FOOT_H, sections=SECTIONS)
    foot = translate(foot, fx, fy, -FOOT_H / 2)
    additive_parts.append(foot)

    # ── Rectangular load cell pocket (cut from bottom of foot) ────────────
    # Goes from Z = -FOOT_H  up to Z = -(FOOT_H - POCKET_H)
    # center Z = -FOOT_H + POCKET_H/2
    pocket = trimesh.creation.box(extents=[POCKET_W, POCKET_D, POCKET_H + 0.1])
    pocket = translate(pocket, fx, fy, -FOOT_H + POCKET_H / 2)
    subtractive_parts.append(pocket)

    # ── M4 through-holes (go through pocket floor + plate) ────────────────
    # Full height = FOOT_H + PLATE_H + 2 mm clearance (extra for clean booleans)
    hole_total_h = FOOT_H + PLATE_H + 2.0
    # Center Z = (-FOOT_H + PLATE_H) / 2  = (-15 + 6)/2 = -4.5
    hole_cz = (-FOOT_H + PLATE_H) / 2

    for dx in [-HOLE_SPACING / 2, +HOLE_SPACING / 2]:
        mhole = trimesh.creation.cylinder(
            radius=M4_HOLE_D / 2, height=hole_total_h, sections=32)
        mhole = translate(mhole, fx + dx, fy, hole_cz)
        subtractive_parts.append(mhole)

    # ── Wire / cable access hole through plate only ───────────────────────
    wire_hole = trimesh.creation.cylinder(
        radius=WIRE_HOLE_D / 2, height=PLATE_H + 2.0, sections=32)
    wire_hole = translate(wire_hole, fx, fy, PLATE_H / 2)
    subtractive_parts.append(wire_hole)


# ── Boolean operations ─────────────────────────────────────────────────────────
print("Unioning base plate + feet …")
solid = trimesh.boolean.union(additive_parts, engine="manifold")

print("Subtracting pockets + holes …")
cutouts = trimesh.boolean.union(subtractive_parts, engine="manifold")
result = trimesh.boolean.difference([solid, cutouts], engine="manifold")

# ── Validate & export ──────────────────────────────────────────────────────────
try:
    result.fix_normals()
except Exception:
    pass
print(f"\nMesh info:")
print(f"  Watertight : {result.is_watertight}")
print(f"  Triangles  : {len(result.faces)}")
bb = result.bounds
print(f"  Bounds X   : {bb[0][0]:.1f} to {bb[1][0]:.1f}  ({bb[1][0]-bb[0][0]:.1f} mm)")
print(f"  Bounds Y   : {bb[0][1]:.1f} to {bb[1][1]:.1f}  ({bb[1][1]-bb[0][1]:.1f} mm)")
print(f"  Bounds Z   : {bb[0][2]:.1f} to {bb[1][2]:.1f}  ({bb[1][2]-bb[0][2]:.1f} mm)")

out_path = "C:/Users/rig2/Downloads/keg_scale_bottom_cover.stl"
result.export(out_path)
print(f"\nSaved to {out_path}")
