#!/usr/bin/env python3
"""Standalone Phase 1–5 checks. Optional DBC input is read-only; never connects to a DB."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--currency-dbc', type=Path)
parser.add_argument('--hunts-epf', type=Path)
parser.add_argument('--extended-cost-dbc', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
tests = {
    'dbc_reader': ['DbcReader', 'DbcDescriptor'],
    'package_lifecycle': [],
    'phase2': ['ContentResourceAllocator', 'ItemDbcComposer', 'DbcReader', 'DbcDescriptor'],
    'occupancy': [],
    'server_bundle': ['ContentServerBundle', 'ContentVendorRow', 'ServerTableDescriptor'],
    'schema2': ['ContentPackage', 'ServerTableDescriptor'],
    'schema3': ['ContentPackage', 'ServerTableDescriptor'],
    'currency': ['CurrencyDbcComposer', 'ContentResourceAllocator', 'ContentServerBundle', 'ContentVendorRow',
                 'ServerTableDescriptor', 'DbcReader', 'DbcDescriptor'],
}
for name in ['server_bundle', 'schema2', 'schema3', 'currency']:
    for unit in ['CurrencyCategoryDbcComposer', 'CurrencyDbcComposer', 'ItemExtendedCostDbc', 'DbcReader', 'DbcDescriptor']:
        if unit not in tests[name]: tests[name].append(unit)
tests['category'] = ['CurrencyCategoryDbcComposer', 'CurrencyDbcComposer', 'DbcReader', 'DbcDescriptor', 'ContentResourceAllocator']
tests['extended_cost'] = ['ItemExtendedCostDbc', 'ContentResourceAllocator', 'ContentPackage', 'ContentServerBundle', 'ContentVendorRow', 'ServerTableDescriptor', 'CurrencyCategoryDbcComposer', 'CurrencyDbcComposer', 'DbcReader', 'DbcDescriptor']
with tempfile.TemporaryDirectory(prefix='content-phase4-tests-') as directory:
    for name, units in tests.items():
        binary = Path(directory) / name
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O0', '-g', '-I' + str(root / 'src'),
                   str(root / 'tests' / (name + '_tests.cpp'))]
        command += [str(root / 'src' / (unit + '.cpp')) for unit in units]
        if name in ['schema2','schema3','extended_cost']:
            command.append(str(root / 'src/third_party/miniz/miniz.c'))
        subprocess.run(command + ['-o', str(binary)], check=True)
        inputs = []
        if name == 'currency' and args.currency_dbc:
            inputs.append(str(args.currency_dbc.resolve()))
        if name == 'schema2' and args.hunts_epf:
            inputs.append(str(args.hunts_epf.resolve()))
        if name == 'extended_cost' and args.extended_cost_dbc:
            inputs.append(str(args.extended_cost_dbc.resolve()))
        subprocess.run([str(binary), *inputs], check=True)
        print(name + ': PASS', flush=True)
