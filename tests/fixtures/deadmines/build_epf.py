#!/usr/bin/env python3
"""Assemble the Deadmines dungeon-map EPF from the authored manifest and local artwork.

The manifest in this directory is the only source of truth. The .blp artwork is
stock client data and is deliberately not committed, so it is supplied on the
command line and copied into the EPF under the manifest's declared source path.

    python3 tests/fixtures/deadmines/build_epf.py \
        --artwork /path/to/Interface/WorldMap/TheDeadmines \
        --output /tmp/mod-deadmines-dungeon-map.epf

The archive is written deterministically: manifest.json first, then each
artwork file in manifest order, with a fixed timestamp and stored (uncompressed)
entries, so two runs over the same inputs produce identical bytes.
"""
import argparse
import json
import pathlib
import zipfile

FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artwork", type=pathlib.Path, required=True,
                        help="Directory holding the Deadmines .blp files.")
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="EPF to write. An existing file is never overwritten.")
    parser.add_argument("--manifest", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().with_name("manifest.json"))
    options = parser.parse_args()

    manifest_bytes = options.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    assert manifest["schema"] == 3, "The Deadmines fixture requires schema 3"

    targets = [item["target"] for item in manifest["content"]]
    assert len(targets) == len(set(targets)), "Duplicate install target"
    expected = {"Interface/WorldMap/TheDeadmines/TheDeadmines%d_%d.blp" % (floor, tile)
                for floor in (1, 2) for tile in range(1, 13)}
    assert set(targets) == expected, "Artwork targets do not match the Deadmines tile set"

    payloads = []
    for item in manifest["content"]:
        name = item["source"].rsplit("/", 1)[-1]
        source = options.artwork / name
        assert source.is_file(), "Missing artwork: " + str(source)
        payloads.append((item["source"], source.read_bytes()))

    if options.output.exists():
        raise SystemExit("Refusing to overwrite " + str(options.output))
    options.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(options.output, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr(zipfile.ZipInfo("manifest.json", FIXED_TIME), manifest_bytes)
        for name, payload in payloads:
            archive.writestr(zipfile.ZipInfo(name, FIXED_TIME), payload)
    print("wrote %s (%d files)" % (options.output, len(payloads) + 1))


if __name__ == "__main__":
    main()
