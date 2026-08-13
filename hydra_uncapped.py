"""Launch Hydra Uncapped: Hydra with Clone Hero's SP meter cap removed.

Clone Hero holds at most 4 bars of Star Power and throws away anything
collected past that, and every path Hydra finds is shaped by it. This build
answers what those paths would be if SP never overfilled: the meter banks as
many bars as the song offers, an activation runs 2 measures per bar spent
with no 8-measure ceiling, and a phrase collected during SP is always worth
its full 2 measures.

The scores it reports are not achievable in Clone Hero. Nothing here is a
better path for an actual run; it's a look at how much the cap is costing and
where.

    python hydra_uncapped.py

It keeps its own library, settings and records (hyapp_uncapped.db,
hyapp_uncapped.ini) so it can be used alongside the normal app without either
one disturbing the other. Charts still have to be scanned and analyzed here
once; capped records are not reusable, and the app marks them "(Update...)".

"""

import os
import pathlib
import runpy

# Read by hydra.hymisc when it is first imported, which is what decides the
# edition for this process -- including which database and ini it opens. It
# has to be set before anything pulls hymisc in, so this module deliberately
# imports nothing from hydra, and runs the app below rather than at the top.
os.environ['HYDRA_UNCAPPED'] = '1'

APP = pathlib.Path(__file__).resolve().parent / "hydra_app.py"

if __name__ == '__main__':
    # hydra_app keeps its startup under "if __name__ == '__main__'", with
    # state the UI callbacks reach as module globals, so it is run as the main
    # program rather than imported and called.
    runpy.run_path(str(APP), run_name='__main__')
