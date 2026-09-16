#!/usr/bin/env python3
"""Standalone Phase 1–4 checks. Optional DBC input is read-only; never connects to a DB."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--currency-dbc', type=Path)
parser.add_argument('--hunts-epf', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
tests = {
    'dbc_reader': ['DbcReader', 'DbcDescriptor'],
    'phase2': ['ContentResourceAllocator', 'ItemDbcComposer', 'DbcReader', 'DbcDescriptor'],
    'occupancy': [],
    'server_bundle': ['ContentServerBundle', 'ServerTableDescriptor'],
    'schema2': ['ContentPackage', 'ServerTableDescriptor'],
    'currency': ['CurrencyDbcComposer', 'ContentResourceAllocator', 'ContentServerBundle',
                 'ServerTableDescriptor', 'DbcReader', 'DbcDescriptor'],
}
with tempfile.TemporaryDirectory(prefix='content-phase4-tests-') as directory:
    for name, units in tests.items():
        binary = Path(directory) / name
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O0', '-g', '-I' + str(root / 'src'),
                   str(root / 'tests' / (name + '_tests.cpp'))]
        command += [str(root / 'src' / (unit + '.cpp')) for unit in units]
        if name == 'schema2':
            command.append(str(root / 'src/third_party/miniz/miniz.c'))
        subprocess.run(command + ['-o', str(binary)], check=True)
        inputs = []
        if name == 'currency' and args.currency_dbc:
            inputs.append(str(args.currency_dbc.resolve()))
        if name == 'schema2' and args.hunts_epf:
            inputs.append(str(args.hunts_epf.resolve()))
        subprocess.run([str(binary), *inputs], check=True)
        print(name + ': PASS', flush=True)
