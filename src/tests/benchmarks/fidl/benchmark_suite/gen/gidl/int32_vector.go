// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
	"gen/types"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "int32_vector.gen.gidl",
		Gen:      gidlGenInt32Vector,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Int32Vector/256",
				Comment: `256 element int32 vector in a struct`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenInt32Vector(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	return fmt.Sprintf(`
Int32Vector{
	values: [
%[1]s
	]
}`, util.List(size, util.SequentialHexValues(types.Int32, 0))), nil
}
