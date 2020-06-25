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
		Filename: "struct_tree.gen.gidl",
		Gen:      gidlGenStructTree,
		Benchmarks: []config.Benchmark{
			{
				Name:    "StructTree/Depth8",
				Comment: `Binary tree with depth 8 (255 elements)`,
				Config: config.Config{
					"depth": 8,
				},
			},
		},
	})
}

func treeValueString(level int) string {
	if level == 1 {
		return `StructTree1{
	a: 1,
	b: 2,
}`
	}
	nextLevel := treeValueString(level - 1)
	return fmt.Sprintf(
		`StructTree%[1]d{
		left:%[2]s,
		right:%[2]s,
	}`, level, nextLevel)
}

func gidlGenStructTree(conf config.Config) (string, error) {
	depth := conf.GetInt("depth")
	return treeValueString(depth), nil
}
