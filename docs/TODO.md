# TODO — Plum-CEC-Firmware

Small work that has no issue yet. This file lives at `docs/TODO.md`.

Anything a user would ask for goes to GitHub instead:

- A feature somebody wants → a feature request issue
- A defect → a bug report issue
- A release worth planning → the roadmap

## Now

The M0 to M6 plan is in `docs/Plans/ProofOfConcept.md`. Its step list is the work queue.

## Next

- [ ] Add host unit tests for `pulse8` framing and escaping in `test/` (plan step 7)

## Someday

- [ ] Document and test the host-state tools for hosts without USB (HASS.Agent, LNXlink,
  the Home Assistant Companion app, System Bridge), with an HA automation that links a
  host state to the Adapter. Later: a guide, or one app for all systems.

- [ ] Upstream PRs to ESPHome (plan step 13, owner approval for each PR)

## Scaffold gaps

- [ ] .github/workflows/Review.yml — the pytest job passes with no tests until tests exist
- [ ] scripts/ — udev rule and systemd unit are untested templates; set the real serial number
- [ ] docs/ARCHITECTURE.md — HTTP authentication is not designed
