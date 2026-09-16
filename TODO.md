# TODO

- [ ] Update README with basis user stuff
- [ ] create CONTRIBUTION with all the links to the docs
- [ ] create a docs/c4.md file with diagrams with c4
- [ ] create dataflow diagrams
- [ ] create class diagram and ability to regenerate the 'model' split from the view
- [ ] create sequeince diagrams for the complex stuff
- [ ] analyse the literals, do we have duplicate stuff that can be cntralized? e.g. output stuff, can we centralize that so we can have themes or something 
- [ ] see repeating patterns, workdir queried mulitpe times maybe even different, write central libs with themed utils, e.g. one 
- [ ] what about srp, doe we have violations
- [ ] the rules, do they follow the iso * iliies structure, would that be applicable
- [ ] run cpm here, do we have dead code, dead docs, do we have links back end forth between code and docs? > run cpm in extreme mode, all should be ok. run semgrep too
- [ ] do we have implicit design choices that we didn't document?
- [ ] do we need all files and folders in the root, can we clean up? like docs/src/rules? can we centraise configs in .config (rename config) is er een standaard voor folder structure voor cpp?
- [ ] kunnen we 'libmode' draaien (semgrep, gitleaks, ...) of 'ruleengine' mode, of allebei? en de results, juiste format zodat we een rapport hebben?
- [ ] kunnen we loopen/leren met ollama? zoals we bedacht hebben?
- [ ] tui/a11y/design, zijn er tui designs, zijn we accessbile? zie knowledge in cpm kunnen we all uitvoer consistent houden en netjes, json output, compact, verbose, 'mooi met kleurtjes' of combi's die logisch zijn?
- [ ] is de rules pluggable, zodat je je private rules kan laden? is de documentatie daarvoor voldoende? zit die in de binary? is er een man pages? dus kan ai dit ook vinden dat je custom rules kan maken
