Run the full keg scale design save workflow:

1. Run the STL generator:
   `C:/Python314/python.exe C:/Users/rig2/Downloads/gen_keg_scale_bottom_cover.py`

2. Copy the generated STL files into the repo:
   - `C:/Users/rig2/Downloads/keg_scale_bottom_cover.stl` → `files/keg_scale_bottom_cover.stl`
   - `C:/Users/rig2/Downloads/keg_scale_feet.stl` → `files/keg_scale_feet.stl`
   - `C:/Users/rig2/Downloads/gen_keg_scale_bottom_cover.py` → `files/gen_keg_scale_bottom_cover.py`

3. Stage only those three files (do not stage .claude/ or other untracked files).

4. Commit with a concise, descriptive message summarising what changed in this iteration. End the commit message with:
   Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>

5. Push to origin.

6. Report the commit hash and a one-line summary of what was saved.
