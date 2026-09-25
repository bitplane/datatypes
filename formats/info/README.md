# Amiga icons

Reads Workbench `.info` icons in every image form: planar images from OS 1.x to 3.1 in Workbench's default pens, NewIcons, OS 3.5 colour icons, and ARGB images from AROS, MorphOS and OS4.

A file can hold up to eight images. The one Workbench would show loads by default, and `PDTA_WhichPicture` picks another.

Saves a project icon with an old-style two-plane image and the picture itself as an OS 3.5 or ARGB image. Pictures larger than 256 pixels a side can't be saved.

Not supported: PNG icons, which the PNG class opens, and PNG chunks inside an icon.

Our `INFO` descriptor matches the icon magic on `.info` files, so GNU texinfo files aren't claimed.
