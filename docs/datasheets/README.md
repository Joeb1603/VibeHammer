# Army roster datasheets (reference)

Army rosters exported as HTML from New Recruit (newrecruit.eu), kept as reference for future unit work
(PLAN.md "Later": more unit types). Open them in a browser. Each one lists the units with their full
datasheets (weapons, abilities, keywords) and the detachment and core stratagems.

| File | Army | Units |
|---|---|---|
| `app_test_tyranids.html` | Tyranids, Invasion Fleet, Incursion (955 pts) | Hive Tyrant, 2 x 10 Termagants, 2 x 5 Genestealers, 3 Tyrant Guard, 2 Screamer-killers, Tyrannofex |
| `app_test_csm.html` | Chaos Space Marines, Renegade Raiders, Incursion (960 pts) | Chaos Lord, 2 x 10 Legionaries, 10 Chaos Terminators, 5 Havocs |

These are reference only. The engine never reads them: unit data still comes from BSData through
`tools/fetch_bsdata.py` into `data/units/` (do not hand-edit those). Like `docs/rules/`, this is
Games Workshop's copyrighted content, committed only because the repository is private. Never publish it
or include it in a release package.
