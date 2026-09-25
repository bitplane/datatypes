# Lunapaint

Reads projects from Lunapaint, AROS's own paint program, and shows each frame with its visible layers flattened. The first frame loads by default, and `PDTA_WhichPicture` picks another.

Saves a one-layer, one-frame project. Pictures wider or taller than 32767 can't be saved.

AROS's own `Devs/DataTypes/Lunapaint` descriptor selects the class by the `Lunapaint_v1` magic.
