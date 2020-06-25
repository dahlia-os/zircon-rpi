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
		Filename: "padded_struct_tree.gen.gidl",
		Gen:      gidlGenPaddedStructTree,
		Benchmarks: []config.Benchmark{
			{
				Name:    "PaddedStructTree/Depth8",
				Comment: `Binary tree with depth 8 (255 elements) with padding on leafs`,
				Config: config.Config{
					"depth": 8,
				},
			},
		},
	})
}

func paddedTreeValueString(level int) string {
	if level == 1 {
		return `PaddedStructTree1{
	a: 1,
	b: 2,
}`
	}
	nextLevel := paddedTreeValueString(level - 1)
	return fmt.Sprintf(
		`PaddedStructTree%[1]d{
		left:%[2]s,
		right:%[2]s,
	}`, level, nextLevel)
}

func gidlGenPaddedStructTree(conf config.Config) (string, error) {
	depth := conf.GetInt("depth")
	return paddedTreeValueString(depth), nil
}
