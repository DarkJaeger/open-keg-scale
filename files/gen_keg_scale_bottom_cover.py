"""
Keg Scale Bottom Cover Generator
=================================
Scale body (existing STL): 210 x 210 x 15 mm
  - Rounded corners, radius = 32 mm (arc centers coincide with foot hole centers)
  - 4 corner holes: ~18 mm dia, centers at (32,32),(178,32),(32,178),(178,178)
  - Load cells mount INSIDE, fixed ends at top, free ends pointing DOWN

Bottom cover design:
  - Rounded-rectangle plate matching the scale body footprint exactly (32 mm corner radius)
  - 4 upward-pointing cylindrical feet at each corner
      * pass up through the 18 mm holes in the scale body bottom
      * dome tip contacts the load cell free end for self-centering
  - 4 rubber-foot recesses on underside at corners for grip pads

Coordinate system:
  Z = 0       = bottom of cover (ground contact)
  Z = PLATE_H = top of cover (where scale body rests)
  Feet extend from Z=0 up to Z = PLATE_H + FOOT_H
"""

import trimesh
import numpy as np
from shapely.geometry import Point
from shapely.ops import unary_union
import trimesh.creation

# ── Parameters ────────────────────────────────────────────────────────────────
SCALE_W       = 210.0   # mm
SCALE_D       = 210.0   # mm
CORNER_R      =  32.0   # mm, matches scale body corner radius
PLATE_H       =   5.0   # mm, cover plate thickness

FOOT_R        =   8.0   # mm radius = 16 mm dia (scale holes ~18 mm)
FOOT_H        =  10.0   # mm, height above cover plate top
DOME_H        =   1.0   # mm, dome on foot tip

FOOT_CENTERS = [
    ( 32.0,  32.0),
    (178.0,  32.0),
    ( 32.0, 178.0),
    (178.0, 178.0),
]

RUBBER_R      =   8.0   # mm, rubber foot recess radius
RUBBER_H      =   2.0   # mm, recess depth
RUBBER_OFFSET =  15.0   # mm inset from corner edges (before rounding)

SECTIONS = 64

# ── Helpers ───────────────────────────────────────────────────────────────────
def translate(mesh, tx, ty, tz):
    mesh.apply_translation([tx, ty, tz])
    return mesh


def rounded_rect_extrusion(w, d, r, h, sections=SECTIONS):
    """Create a solid rounded-rectangle extrusion using shapely + trimesh."""
    # Build shapely polygon: 4 circles at corners + hull
    corners = [
        Point(r,     r    ),
        Point(w - r, r    ),
        Point(r,     d - r),
        Point(w - r, d - r),
    ]
    poly = unary_union([c.buffer(r, resolution=sections // 4) for c in corners]).convex_hull
    # Extrude
    mesh = trimesh.creation.extrude_polygon(poly, h)
    return mesh


def dome_cap(cx, cy, z_base, radius, dome_h, sections=SECTIONS):
    """Cone-shaped dome cap on top of a cylinder."""
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


# ── Build geometry ────────────────────────────────────────────────────────────
additive    = []
subtractive = []

# 1. Rounded-rectangle base plate
plate = rounded_rect_extrusion(SCALE_W, SCALE_D, CORNER_R, PLATE_H, SECTIONS)
additive.append(plate)

# 2. Upward feet + dome caps
for (fx, fy) in FOOT_CENTERS:
    total_h = PLATE_H + FOOT_H
    foot = trimesh.creation.cylinder(radius=FOOT_R, height=total_h, sections=SECTIONS)
    foot = translate(foot, fx, fy, total_h / 2)
    additive.append(foot)

    dome = dome_cap(fx, fy, PLATE_H + FOOT_H, FOOT_R, DOME_H)
    additive.append(dome)

# 3. Rubber foot recesses (underside pockets near corners)
rubber_positions = [
    (RUBBER_OFFSET,           RUBBER_OFFSET),
    (SCALE_W - RUBBER_OFFSET, RUBBER_OFFSET),
    (RUBBER_OFFSET,           SCALE_D - RUBBER_OFFSET),
    (SCALE_W - RUBBER_OFFSET, SCALE_D - RUBBER_OFFSET),
]
for (rx, ry) in rubber_positions:
    recess = trimesh.creation.cylinder(radius=RUBBER_R, height=RUBBER_H + 0.1, sections=32)
    recess = translate(recess, rx, ry, RUBBER_H / 2)
    subtractive.append(recess)

# ── Boolean ops ───────────────────────────────────────────────────────────────
print("Unioning main body ...")
watertight = [m for m in additive if m.is_watertight]
solid = trimesh.boolean.union(watertight, engine="manifold")

print("Subtracting recesses ...")
cuts = trimesh.boolean.union(subtractive, engine="manifold")
result = trimesh.boolean.difference([solid, cuts], engine="manifold")

# ── Report ────────────────────────────────────────────────────────────────────
print(f"\nMesh info:")
print(f"  Watertight : {result.is_watertight}")
print(f"  Triangles  : {len(result.faces)}")
bb = result.bounds
print(f"  X : {bb[0][0]:.1f} to {bb[1][0]:.1f}  ({bb[1][0]-bb[0][0]:.1f} mm)")
print(f"  Y : {bb[0][1]:.1f} to {bb[1][1]:.1f}  ({bb[1][1]-bb[0][1]:.1f} mm)")
print(f"  Z : {bb[0][2]:.1f} to {bb[1][2]:.1f}  ({bb[1][2]-bb[0][2]:.1f} mm)")
print(f"\nCorner radius  : {CORNER_R:.0f} mm (matches scale body)")
print(f"Foot diameter  : {FOOT_R*2:.0f} mm  (scale holes ~18 mm)")
print(f"Foot protrusion: {FOOT_H:.0f} mm above plate top")

out = "C:/Users/rig2/Downloads/keg_scale_bottom_cover.stl"
result.export(out)
print(f"\nSaved to {out}")
