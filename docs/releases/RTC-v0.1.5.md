# custom-v0.1.5 RTC compatibility

Back up the game's `.sav` and every `.g3rtc*` sibling before updating. The save
format is unchanged. Fresh games and valid modern `.g3rtc2` files need no
migration. Do not delete RTC files to bypass a startup notice.

Legacy `.g3rtc`, `.g3rtc.tmp` and `.g3rtc.bak` records may contain timestamps
affected by the old BCD masks. The original wall clock cannot be reconstructed
reliably. The only supported transition explicitly adopts one stored game-time
snapshot and anchors it to the DS clock on the next successful startup. It does
not add guessed past elapsed time or use the PC clock.

The included Python 3 tool defaults to read-only inspection. Use local paths;
ROM or save uploads are not required. Supply all three expected legacy paths,
including paths for absent siblings:

```text
python tools/rtc_migrate.py inspect --rom game.gba --legacy-primary game.g3rtc --legacy-temp game.g3rtc.tmp --legacy-backup game.g3rtc.bak
```

After reviewing the report, select an existing valid source (`primary`, `temp`
or `backup`). Only if you accept adopting its stored snapshot, use its exact
reported SHA-256 with a separate, nonexistent output:

```text
python tools/rtc_migrate.py adopt-stored-snapshot --rom game.gba --legacy-primary game.g3rtc --legacy-temp game.g3rtc.tmp --legacy-backup game.g3rtc.bak --source primary --expected-sha256 REPLACE_WITH_REPORTED_SHA256 --output game.g3rtc2
```

Keep the original legacy siblings byte-identical and place the new record in the
same game sidecar location. Existing outputs are refused. Do not overwrite an
existing modern set. On startup, the loader commits and rereads the new DS-clock
anchor before allowing gameplay. Conflicts, unsupported versions and I/O errors
stop startup rather than resetting time silently. Retain files and report the
notice if blocked; the tool is not a generic corruption repair utility.

For rollback to custom-v0.1.4, restore the matching pre-upgrade backup set with
the old executable. New RTC progress is not imported into the old format.
Changes to legacy files after downgrade can block a later upgrade. Never use
SD removal or power-cut tests with valuable saves.
