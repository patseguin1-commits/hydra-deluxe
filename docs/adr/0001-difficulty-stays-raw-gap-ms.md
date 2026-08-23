# Difficulty stays raw gap ms; transfer scaling is display-only

A squeeze's difficulty — the number the search's ms filter, the path list, and
the report tiers all use — is the raw gap in chart ms, never rescaled by the
frontend transfer scale. 1.5.0 briefly shipped a per-hit scaled difficulty and
it was reverted on user demand: the raw gap is the number players verify
against a chart, and rescaling it changed every user-visible timing at once.
The transfer scale appears only as display-layer detail (the `x0.51` warning
and the `eff.` column in the details view — see `core/squeeze_rating.h`).
Do not "fix" difficulty to account for the scale; that is the rejected option.
