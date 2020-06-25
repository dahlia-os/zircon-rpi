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
		Filename: "float_array.gen.gidl",
		Gen:      gidlGenFloatArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "FloatArray/256",
				Comment: `256 float array in a struct`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenFloatArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	return fmt.Sprintf(`
FloatArray%[1]d{
	values: [
%[2]s
	]
}`, size, util.List(size, util.RandomValues(types.Float32))), nil
}
