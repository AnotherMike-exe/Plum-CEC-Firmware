# Local copy of the ESPHome hdmi_cec component

`components/hdmi_cec/` is a copy of the official ESPHome `hdmi_cec` component (merged to
ESPHome `dev` on 2026-10-15), with the same name, so ESPHome uses our copy in place of
its own. We add a runtime ACK mask and a TX task, and send each change upstream as a PR.
Do not rename it: the same name is what makes the override work. Delete the copy when an
ESPHome release contains our changes.
