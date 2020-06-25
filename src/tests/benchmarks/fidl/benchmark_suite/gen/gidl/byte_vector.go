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
		Filename: "byte_vector.gen.gidl",
		Gen:      gidlGenByteVector,
		Benchmarks: []config.Benchmark{
			{
				Name:    "ByteVector/16",
				Comment: `16 byte vector in a struct`,
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Name:    "ByteVector/256",
				Comment: `256 byte vector in a struct`,
				Config: config.Config{
					"size": 256,
				},
				EnableSendEventBenchmark: true,
			},
			{
				Name:    "ByteVector/4096",
				Comment: `4096 byte vector in a struct`,
				Config: config.Config{
					"size": 4096,
				},
				EnableSendEventBenchmark: true,
			},
			{
				Name: "ByteVector/65536",
				Comment: `65536 byte vector in a struct
			Disabled on HLCPP / LLCPP due to clang performance issues`,
				Config: config.Config{
					"size": 65536,
				},
				Denylist: []config.Binding{config.HLCPP, config.LLCPP, config.Walker},
			},
		},
	})
}

func gidlGenByteVector(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	return fmt.Sprintf(`
ByteVector{
	bytes: [
%[1]s
	]
}`, util.List(size, util.SequentialHexValues(types.Uint8, 0))), nil
}
