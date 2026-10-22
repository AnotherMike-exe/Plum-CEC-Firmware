# Proxy or Guest, and the physical address from the EDID

The Adapter acts like a standard CEC device, not a fixed test setup. Its physical address
comes from the EDID, and its logical address comes from a claim by polling. Its role
depends on the Source:

- **Proxy** (the Source has no CEC, for example most PCs): the Adapter claims a Playback
  logical address (4, then 8, then 11), reports the physical address, and answers with its
  OSD name (default `Plum-CEC`, the config can change it), vendor ID and CEC version.
- **Guest** (another device reports our physical address): the Adapter claims no logical
  address. It sends as Unregistered (15) by default. It sends Image View On and Active
  Source with the Source's logical address and physical address, because the CEC spec does
  not allow Active Source from 15. It uses the Source's address only for commands with no
  reply, because a reply goes to the Source, and the Source did not ask for it.

The Adapter reads the EDID passively on the DDC Taps when the Source reads it, takes the
physical address from the HDMI vendor block, and stores it in flash. At boot it uses the
stored address. If it has none, it uses the config value. While Attached, the Adapter gives
the physical address to the kernel through the Pulse-Eight config, and the kernel claims
the logical address (ADR 0001 still applies).

## Considered options

- **A fixed physical address and logical address in the config.** Rejected: the Adapter
  must work on any TV input without a config change.
- **An HDMI repeater with EDID rewrite**, so that the Adapter gets its own physical
  address. Rejected in hardware ADR 0004.
- **A Proxy claim next to a Source with its own CEC.** Rejected: two devices with the same
  physical address confuse the TV, and an Active Source from either one selects the same
  input.

## Consequences

- An EDID read happens only at hot-plug or at Source boot. An Adapter that starts after the
  Source uses the stored or the config address until the next read.
- The Guest role needs a bench check: which commands the Sony accepts from 15 (plan step 14).
