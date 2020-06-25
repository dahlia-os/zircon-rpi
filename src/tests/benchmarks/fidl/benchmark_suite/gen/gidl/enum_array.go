// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
	"strings"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "enum_array.gen.gidl",
		Gen:      gidlGenEnumArray,
		Benchmarks: []config.Benchmark{
			{
				Name: "EnumArray/256",
				Comment: `256 enum array in a struct
				Disabled on LLCPP / Walker because of enum bug in GIDL`,
				Config: config.Config{
					"size": 256,
				},
				Denylist: []config.Binding{config.LLCPP, config.Walker},
			},
		},
	})
}

func gidlGenEnumArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	if size%2 != 0 {
		panic("expected even size")
	}

	return fmt.Sprintf(`
EnumArray%[1]d{
	values: [
%[2]s
	]
}`, size, strings.Repeat("1,\n2,\n", size/2)), nil
}
