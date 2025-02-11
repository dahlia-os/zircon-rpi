#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
import argparse
import os
import string
import sys

# Root dir is 5 levels up from here.
FUCHSIA_DIR = os.path.abspath(
    os.path.join(
        __file__, os.pardir, os.pardir, os.pardir, os.pardir, os.pardir))
sys.path += [os.path.join(FUCHSIA_DIR, 'third_party')]
from jinja2 import Environment, FileSystemLoader


def to_camel_case(snake_str):
    components = snake_str.split('_')
    return ''.join(x.title() for x in components[0:])


def wrap_deps(dep):
    return {'enum': to_camel_case(dep), 'lib': dep}


def main(args_list=None):
    parser = argparse.ArgumentParser(description='Generate FFX Plugin matcher')

    parser.add_argument(
        '--out', help='The output file to generate', required=True)

    parser.add_argument(
        '--deps',
        help='Comma-seperated libraries to generate code from',
        required=True)

    parser.add_argument('--args', help='args lib', required=True)

    parser.add_argument('--sub_command', help='sub command lib', required=True)

    parser.add_argument(
        '--not_complete',
        type=bool,
        help='is the command match incomplete',
        required=False,
        default=False)

    if args_list:
        args = parser.parse_args(args_list)
    else:
        args = parser.parse_args()

    template_path = os.path.join(os.path.dirname(__file__), 'templates')
    env = Environment(
        loader=FileSystemLoader(template_path),
        trim_blocks=True,
        lstrip_blocks=True)
    template = env.get_template('plugins.md')
    libraries = args.deps.split(',')
    plugins = map(wrap_deps, libraries)
    with open(args.out, 'w') as file:
        file.write(
            template.render(
                plugins=plugins,
                suite_subcommand_lib=args.sub_command,
                suite_args_lib=args.args,
                not_complete=args.not_complete))


if __name__ == '__main__':
    sys.exit(main())
