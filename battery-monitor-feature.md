# Support Victron Battery Monitor Advertisements

## Summary
- Parse extra manufacturer data record type `0x02` (Battery Monitor) alongside the existing `0x01` Solar Charger handling.
- Surface Battery Monitor metrics (TTG, voltages, current, consumed Ah, SOC, aux input) in the UI and align copy/error text with Battery Monitor semantics.
- Provide tests or sample payloads to validate both record types.

## Motivation
- Owners of Victron Battery Monitor hardware currently see no telemetry because advertisements are discarded by the BLE parser (`victronRecordType != 0x01`).
- The doc at `docs/extra-manufacturer-data-2022-12-14.pdf` outlines the Battery Monitor record layout; implementing support expands compatibility without requiring firmware changes on the Victron side.

## Success Criteria
- BLE layer accepts both record types `0x01` and `0x02`, identifies payload length, and parses field bit ranges per specification.
- The data model distinguishes device class payloads (e.g. tagged union) and exposes normalized units (V, A, Ah, %, minutes).
- UI renders appropriate labels/values for Battery Monitor devices and continues to show Solar Charger data unchanged.
- Automated or manual tests confirm correct decoding of representative Battery Monitor frames (including TTG, consumed Ah, SOC).
- Documentation (README or in-code comments) points maintainers to the spec section for each field mapping.

## Implementation Notes
- Update `main/victron_ble.c` to branch on `victronRecordType` before copying decrypted bytes; write helpers to extract non byte-aligned fields (`batteryCurrent` 22 bits, `consumedAh` 20 bits, `soc` 10 bits).
- Refactor `victronPanelData_t` in `main/victron_ble.h` into a struct with a record-type tag and typed payloads (e.g. `victron_solar_data_t`, `victron_battery_monitor_data_t`).
- Extend the BLE callback signature or introduce a new one so consumers know which payload they receive; migrate `ui_on_panel_data` accordingly.
- Adjust UI rendering (`main/ui.c`) to display Battery Monitor values and hide Solar Charger-only labels when not applicable.
- Capture or synthesize Battery Monitor advertisement bytes for regression tests; add a simple decoder unit test if feasible.

## Open Questions
- Should the UI detect Battery Monitor devices automatically and switch tabs/layouts, or offer a combined view that labels unavailable fields?
- Do we need to expose alarm reason codes in human-readable form similar to charger error strings, and if so, is there a reference mapping?

Battery Monitor Support

main/victron_ble.c:100-103 hard-filters decrypted payloads to record type 0x01 (solar charger) and drops everything else; add routing for record type 0x02 (“Battery Monitor”) and leave vendor/key checks in place.
The decrypted bytes are blindly copied into victronPanelData_t (main/victron_ble.c:127-129), and the follow-up sentinel check on outputCurrentHi assumes the solar charger layout; replace this with a record-type-aware parser. Battery-monitor frames are 20 bytes and pack several non byte-aligned fields (TTG @ 1‑min steps, current in mA, consumed Ah, SOC, etc., per docs/extra-manufacturer-data-2022-12-14.txt:266-337), so you’ll need helpers to extract bit fields before normalizing units.
Redefine the data model in main/victron_ble.h:12-26: either add a record_type plus a union of typed payloads (e.g. victron_solar_data_t vs. victron_battery_monitor_data_t), or introduce a new callback signature that passes both the type and decoded values. Keep the API stable for existing solar users or migrate all call sites accordingly.
main/ui.c:408-434 formats the solar-only metrics (PV watts, yield, charger state/error text); guard those with the record type and introduce a Battery Monitor view (TTG, current, voltage, SOC, consumed Ah, aux input/temperature, alarm reason). You’ll also need alternate error/state copy—battery monitors don’t provide the charger state or the same error codes.
Add validation paths: canned advertisements for both device classes make it easier to unit-test the new parser logic, and UI smoke tests (or screenshots) should confirm the new layout. Consider documenting the mapping you implement so future device classes can follow the same pattern.

Next steps: 1) refactor the BLE parser and data structures to deliver record-type-specific payloads; 2) extend the UI layer with a Battery Monitor layout and state handling; 3) test with captured Battery Monitor advertisements (or synthetic vectors) to verify parsing and rendering.