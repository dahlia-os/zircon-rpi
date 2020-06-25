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
		Filename: "struct_array.gen.gidl",
		Gen:      gidlGenStructArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "StructArray/256",
				Comment: `256 element array of structs`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenStructArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	elem := `
StructArrayElement{
	a: 1,
	b: 2
},`

	return fmt.Sprintf(`
StructArray%[1]d{
	elems: [
%[2]s
	]
}`, size, strings.Repeat(elem, size)), nil
}
