# Next stages (research notes — NOT in the application)

**English** · [Türkçe](NEXT_STAGES.tr.md)

> This file only records work that **could** be considered in the future. **None** of it exists in MONSTER GPU POWER, none of it has been touched and none of it is promised:
> there is no shared CPU/GPU power budget, no Control Center alternative and no Advanced BIOS change in the code; the 230 W adapter policy was not changed; the 330 W profile was not returned to.
> Prerequisite: the [device acceptance test](DEVICE_ACCEPTANCE_TEST.md) must have been passed on your own device.

## Stage 2 — Shared CPU/GPU power budget

* **Observation:** while the RTX 3070 Laptop GPU is at 150 W the i7-11800H appears to drop to about **60 W**; at a GPU power of 140 W the CPU gets more power. The cause is unknown; no assumption will be made.
* **To investigate (starting read-only):** the EC CPU limits `0x0730` / `0x0731` and `0x0783` / `0x0784` / `0x0785`; the shared **AC / PSYS / charger / VRM** budget; which component (EC, BIOS, GCUService, Intel DTT/PSYS) decides the share.
* In the **230 W profile** the real source of power sharing will be researched **with evidence**; **the physical adapter capacity will not be exceeded**.
* **No return to the 330 W profile:** it will not be tried again because the ACPI battery metric broke in the 330 W profile before (an unknown 330 W limitation). This project does not claim to solve ACPI battery problems.
* Acceptance criterion (proposal): for every EC address, a read-only log + evidence first; writing only with user confirmation and a rollback plan.

## Stage 3 — A light "Control Center alternative"

* A light application that combines **all** verified GPU / CPU / fan / EC functions. It would be a **separate development project** and would not replace this application.
* It would carry this application's safety principles: allow-list only, read-back, verification, rollback, locking, honest status texts.

## Stage 4 — BIOS `N.1.07Mon08` hidden "Advanced" menu

* Inspecting the AMITSE form filter and IFR conditions — a **separate, high-risk** investigation.
* **OEM BIOS/EC files are not touched by this application.** Before any such work starts, a backup and a recovery plan (programmer, recovery method) must be ready.
