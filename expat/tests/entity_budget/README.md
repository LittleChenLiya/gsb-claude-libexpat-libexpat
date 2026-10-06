# Entity expansion budget tests

Targeted tests for `XML_SetEntityExpansionBudget`:

- `fixtures/internal_ok.xml` — small internal entity, parses within budget
- `fixtures/internal_over_budget.xml` — nested internal entities, exceeds a
  small budget; parsing stops with `XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED`
  and the parser can be freed/reset safely
- `fixtures/external_file.xml` — references external file entity
  `entity_payload.txt`; with a budget active it is rejected up front with
  `XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED` and the external entity handler is
  never invoked, so no file (or network) access happens
- without a budget, the original parser behavior is preserved

Build and run (native compiler, no Docker):

```sh
sh run_tests.sh            # uses cc, falls back to gcc; override with CC=...
```
