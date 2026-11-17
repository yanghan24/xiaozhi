# Domain Docs

## Layout

This repository uses a single context: `GLOSSARY.md` at the repository root and architecture decision records in `docs/adr/`.

Create these files lazily when a domain term or architectural decision is resolved, using the `domain-modeling` skill.

## Before exploring the codebase

Read `GLOSSARY.md` and any ADRs in `docs/adr/` relevant to the area you are exploring. If they do not exist, proceed silently.

## Use the glossary's vocabulary

Use the terms defined in `GLOSSARY.md` when naming domain concepts in issue titles, proposals, hypotheses, and tests. If a needed concept is missing, check whether the project already uses another term; record a real gap for `domain-modeling`.

## Flag ADR conflicts

If a proposal contradicts an existing ADR, explicitly identify the ADR and explain why the decision should be revisited.
