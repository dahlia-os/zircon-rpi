#!/usr/bin/env python3
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to fetch missing LICENSE files when building a vendored repo

# Should be run from top-level of third_party/rust_crates repository.

import argparse
import json
import os
import re
import sys
import urllib.request

repo_re = re.compile('\s*repository\s*=\s*"(.*)"\s*$')


def die(reason):
    raise Exception(reason)


def get_repo_path(subdir):
    for line in open(os.path.join(subdir, 'Cargo.toml')):
        m = repo_re.match(line)
        if m:
            return m.group(1)


def get_github_content(path):
    s = path.split('/')
    if s[2] == 'github.com':
        s[2] = 'api.github.com/repos'
        # github redirects "github.com/$USER/$PROJECT.git" to
        # "github.com/$USER/$PROJECT".
        if s[4].endswith('.git'):
            s[4] = s[4][:len('.git')]
    else:
        die('don\'t know raw content path for ' + path)
    if s[-1] == '':
        del s[-1]
    if len(s) >= 6 and s[5] == 'tree':
        del s[5]
    else:
        s.append('contents')
    # unprivildged API token to avoid API limits, since API limits are imposed
    # based on the originating IP address, and only allow 60 requests per hour.
    #
    # https://developer.github.com/v3/#rate-limiting
    #
    # TODO: can we do better?
    token = '84509a5cf729275dee7ab7363be1a6147adf3183'
    return json.load(
        urllib.request.urlopen(
            urllib.request.Request(
                '/'.join(s), headers={
                    'Authorization': 'token %s' % token,
                })))


def fetch_license(subdir):
    repo_path = get_repo_path(subdir)
    if repo_path is None:
        die('can\'t find repo path for ' + subdir)
    content = get_github_content(repo_path)
    first = True
    with open(os.path.join(subdir, 'LICENSE'), 'wb') as license_out:
        for file in content:
            # 'LICENCE' is a British English spelling variant used in https://github.com/ebarnard/rust-plist
            if file['name'] in ('LICENSE', 'LICENSE-APACHE', 'LICENSE-MIT',
                                'COPYING', 'LICENCE', 'LICENSE.md'):
                if first:
                    first = False
                else:
                    license_out.write(bytearray('=' * 40 + '\n', 'utf-8'))

                url = file['download_url']
                license_out.write(bytearray(url + ':\n\n', 'utf-8'))
                with urllib.request.urlopen(url) as resp:
                    license_out.write(resp.read())
    if first:
        die('no licenses found under ' + repo_path)


def check_licenses(directory, verify=False):
    success = True
    os.chdir(directory)
    for subdir in sorted(os.listdir(os.getcwd())):
        # TODO(pylaligand): remove this temporary hack when a new version of
        # the crate is published.
        if subdir.startswith('fuchsia-zircon-sys'):
            print('IGNORED  %s' % subdir)
            continue
        if subdir.startswith('.') or not os.path.isdir(subdir):
            continue
        license_files = [
            file for file in os.listdir(subdir) if file.startswith('LICENSE') or
            file.startswith('LICENCE') or file.startswith('license')
        ]
        if license_files:
            print('OK       %s' % subdir)
            continue
        if verify:
            print('MISSING  %s' % subdir)
            success = False
            continue
        try:
            fetch_license(subdir)
            print('FETCH    %s' % subdir)
        except Exception as err:
            print('ERROR    %s (%s)' % (subdir, err.message))
            success = False
    return success


def main():
    parser = argparse.ArgumentParser(
        'Verifies licenses for third-party Rust crates')
    parser.add_argument(
        '--directory',
        help='Directory containing the crates',
        default=os.getcwd())
    parser.add_argument(
        '--verify',
        help='Simply check whether licenses are up-to-date',
        action='store_true')
    args = parser.parse_args()
    if not check_licenses(args.directory, verify=args.verify):
        sys.exit(1)


if __name__ == '__main__':
    main()
