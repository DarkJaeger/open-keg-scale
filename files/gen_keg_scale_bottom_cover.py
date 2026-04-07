"""
Keg Scale Bottom Cover Generator  (revised)
============================================
Scale body (existing STL): 210 x 210 x 15 mm
  - 4 corner holes: ~18 mm dia, centers at approx (32,32),(178,32),(32,178),(178,178)
  - Load cells mount INSIDE, fixed ends at top, free ends pointing DOWN

Bottom cover design:
  - Flat plate: 210 x 210 x PLATE_H mm  (sits on the ground)
  - 4 upward-pointing cylindrical feet at each corner
      * pass up through the holes in the scale body bottom
      * tips contact the load cell free ends
  - Each foot has a slight dome top for self-centering on the load cell
  - 4 small rubber-foot dimples on the underside for grip/traction
  - Central wire-pass hole (optional)

Coordinate system:
  Z = 0      = bottom of cover (ground contact)
  Z = PLATE_H = top of cover plate (where scale body sits)
  Feet extend from Z=0 up to Z = PLATE_H + FOOT_H
"""

import trimesh
import numpy as np

# ── Parameters (adjust as needed) ────────────────────────────────────────────
SCALE_W       = 210.0   # mm
SCALE_D       = 210.0   # mm
PLATE_H       =   5.0   # mm, cover plate thickness

# Upward feet (pass through scale body holes to contact load cells)
FOOT_R        =   8.0   # mm radius = 16 mm dia  (scale body holes are ~18 mm dia)
FOOT_H        =  10.0   # mm, foot height ABOVE cover plate top
                        # Adjust so foot tip meets load cell free end inside housing
DOME_H        =   1.0   # mm, dome height on foot tip for self-centering

# Foot center positions (matched to scale body corner hole centers)
FOOT_CENTERS = [
    ( 32.0,  32.0),
    (178.0,  32.0),
    ( 32.0, 178.0),
    (178.0, 178.0),
]

# Small rubber-foot dimples on underside at corners (ground contact)
RUBBER_R      =   8.0   # mm
RUBBER_H      =   2.0   # mm, depth of rubber foot recess
RUBBER_OFFSET =  15.0   # mm from corner edges

# Sections for cylinder approximation
SECTIONS = 64

# ── Helpers ───────────────────────────────────────────────────────────────────
def translate(mesh, tx, ty, tz):
    mesh.apply_translation([tx, ty, tz])
    return mesh


def dome_cap(cx, cy, z_base, radius, dome_h, sections=SECTIONS):
    """Approximate spherical dome as a fan of triangles on top of a cylinder."""
    angles = np.linspace(0, 2 * np.pi, sections, endpoint=False)
    rim = np.column_stack([
        cx + radius * np.cos(angles),
        cy + radius * np.sin(angles),
        np.full(sections, z_base),
    ])
    tip = np.array([[cx, cy, z_base + dome_h]])
    verts = np.vstack([rim, tip])

    tip_idx = sections
    faces = np.array([
        [i, (i + 1) % sections, tip_idx]
        for i in range(sections)
    ], dtype=np.int32)
    return trimesh.Trimesh(vertices=verts, faces=faces, process=False)


# ── Build geometry ────────────────────────────────────────────────────────────
additive = []
subtractive = []

# 1. Base plate
plate = trimesh.creation.box(extents=[SCALE_W, SCALE_D, PLATE_H])
plate = translate(plate, SCALE_W / 2, SCALE_D / 2, PLATE_H / 2)
additive.append(plate)

for (fx, fy) in FOOT_CENTERS:

    # 2. Upward cylindrical foot  (Z = 0 to PLATE_H + FOOT_H)
    total_cyl_h = PLATE_H + FOOT_H
    foot = trimesh.creation.cylinder(radius=FOOT_R, height=total_cyl_h, sections=SECTIONS)
    foot = translate(foot, fx, fy, total_cyl_h / 2)
    additive.append(foot)

    # 3. Dome on foot tip
    dome = dome_cap(fx, fy, PLATE_H + FOOT_H, FOOT_R, DOME_H)
    additive.append(dome)

# 4. Rubber foot recesses on underside (small cylindrical pockets)
rubber_positions = [
    (RUBBER_OFFSET,           RUBBER_OFFSET),
    (SCALE_W - RUBBER_OFFSET, RUBBER_OFFSET),
    (RUBBER_OFFSET,           SCALE_D - RUBBER_OFFSET),
    (SCALE_W - RUBBER_OFFSET, SCALE_D - RUBBER_OFFSET),
]
for (rx, ry) in rubber_positions:
    recess = trimesh.creation.cylinder(radius=RUBBER_R, height=RUBBER_H + 0.1, sections=32)
    recess = translate(recess, rx, ry, RUBBER_H / 2)   # sits at bottom of plate
    subtractive.append(recess)

# ── Boolean ops ───────────────────────────────────────────────────────────────
print("Unioning main body ...")
# Filter to watertight meshes only for union
watertight = [m for m in additive if m.is_watertight]
non_watertight = [m for m in additive if not m.is_watertight]

solid = trimesh.boolean.union(watertight, engine="manifold")

if subtractive:
    print("Subtracting recesses ...")
    cuts = trimesh.boolean.union(subtractive, engine="manifold")
    result = trimesh.boolean.difference([solid, cuts], engine="manifold")
else:
    result = solid

# ── Report ────────────────────────────────────────────────────────────────────
print(f"\nMesh info:")
print(f"  Watertight : {result.is_watertight}")
print(f"  Triangles  : {len(result.faces)}")
bb = result.bounds
print(f"  X : {bb[0][0]:.1f} to {bb[1][0]:.1f}  ({bb[1][0]-bb[0][0]:.1f} mm)")
print(f"  Y : {bb[0][1]:.1f} to {bb[1][1]:.1f}  ({bb[1][1]-bb[0][1]:.1f} mm)")
print(f"  Z : {bb[0][2]:.1f} to {bb[1][2]:.1f}  ({bb[1][2]-bb[0][2]:.1f} mm)")
print(f"\nFoot tip height above cover plate : {FOOT_H:.1f} mm")
print(f"Foot diameter                      : {FOOT_R*2:.1f} mm")
print(f"  (scale body holes are ~18 mm dia, feet are {FOOT_R*2:.1f} mm dia)")

out = "C:/Users/rig2/Downloads/keg_scale_bottom_cover.stl"
result.export(out)
print(f"\nSaved to {out}")
