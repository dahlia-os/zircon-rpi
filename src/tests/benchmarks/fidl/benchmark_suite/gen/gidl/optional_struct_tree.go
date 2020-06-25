// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "optional_struct_tree.gen.gidl",
		Gen:      gidlGenOptionalStructTree,
		Benchmarks: []config.Benchmark{
			{
				Name:    "OptionalStructTree/Depth8",
				Comment: `Binary tree with depth 8 composed of recursive optional nodes (255 elements)`,
				Config: config.Config{
					"depth": 8,
				},
				// Go runs into a stack overflow.
				// Dart doesn't support recursive type generation.
				Denylist: []config.Binding{config.Go, config.Dart},
			},
		},
	})
}

func optionalTreeValueString(level int) string {
	if level == 1 {
		return `OptionalStructTree{}`
	}
	nextLevel := optionalTreeValueString(level - 1)
	return fmt.Sprintf(
		`OptionalStructTree{
		left:%[2]s,
		right:%[2]s,
	}`, level, nextLevel)
}

func gidlGenOptionalStructTree(conf config.Config) (string, error) {
	depth := conf.GetInt("depth")
	return optionalTreeValueString(depth), nil
}
