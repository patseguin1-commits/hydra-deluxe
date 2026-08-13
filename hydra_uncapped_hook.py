"""PyInstaller runtime hook that makes HydraUncapped.exe the uncapped edition.

Runtime hooks run before the bundled program does, which is the only moment
this can be set: hydra.hymisc reads the environment once, on import, and that
single read decides the SP rule, the record version and which database the
app opens.

Running from source uses hydra_uncapped.py instead; both exist because the
frozen app has no launcher script in front of it.

"""

import os

os.environ['HYDRA_UNCAPPED'] = '1'
