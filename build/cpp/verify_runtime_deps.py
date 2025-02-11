#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def has_packaged_file(needed_file, deps):
    """Returns true if the given file could be found in the given deps."""
    for dep in deps:
        for file in dep['files']:
            if needed_file == os.path.normpath(file['source']):
                return True
    return False


def has_missing_files(runtime_files, package_deps):
    """Returns true if a runtime file is missing from the given deps."""
    has_missing_files = False
    for file in runtime_files:
        # Some libraries are only known to GN as ABI stubs, whereas the real
        # runtime dependency is generated in parallel as a ".so.impl" file.
        if (not has_packaged_file(file, package_deps) and
                not has_packaged_file('%s.impl' % file, package_deps)):
            print('No package dependency generates %s' % file)
            has_missing_files = True
    return has_missing_files


def main():
    parser = argparse.ArgumentParser(
        "Verifies a prebuilt library's runtime dependencies")
    parser.add_argument(
        '--root-build-dir',
        help='Path to the root build directory',
        required=True)
    parser.add_argument(
        '--runtime-deps-file',
        help='Path to the list of runtime deps',
        required=True)
    parser.add_argument(
        '--manifest',
        help='Path to the target\'s SDK manifest file',
        required=True)
    parser.add_argument(
        '--stamp', help='Path to the stamp file to generate', required=True)
    args = parser.parse_args()

    # Read the list of runtime dependencies generated by GN.
    def normalize_dep(dep):
        return os.path.normpath(os.path.join(args.root_build_dir, dep.strip()))

    with open(args.runtime_deps_file, 'r') as runtime_deps_file:
        runtime_files = list(map(normalize_dep, runtime_deps_file.readlines()))

    # Read the list of package dependencies for the library's SDK incarnation.
    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)
    atom_id = manifest['ids'][0]

    def find_atom(id):
        return next(a for a in manifest['atoms'] if a['id'] == id)

    atom = find_atom(atom_id)
    deps = [find_atom(a) for a in atom['deps']]
    deps += [atom]

    # Check whether all runtime files are available for packaging.
    if has_missing_files(runtime_files, deps):
        return 1

    with open(args.stamp, 'w') as stamp:
        stamp.write('Success!')


if __name__ == '__main__':
    sys.exit(main())
