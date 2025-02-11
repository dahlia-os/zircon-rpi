// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	libllcpp "gidl/llcpp/lib"
	gidlmixer "gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("tmpl").Parse(`
#include <benchmarkfidl/llcpp/fidl.h>
#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/llcpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/encode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/send_event_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/memcpy_benchmark_util.h"

namespace {

{{ .Type }} Build{{ .Name }}Heap() {
	{{ .ValueBuildHeap }}
	auto obj = {{ .ValueVarHeap }};
	return obj;
}
bool BenchmarkBuilder{{ .Name }}Heap(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }}Heap);
}
{{ if .EnableSendEventBenchmark }}
bool BenchmarkSendEvent{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::SendEventBenchmark<{{ .EventProtocolType }}>(state, Build{{ .Name }}Heap);
}
{{- end -}}
bool BenchmarkMemcpy{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::MemcpyBenchmark(state, Build{{ .Name }}Heap);
}

void RegisterTests() {
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/WallTime", BenchmarkBuilder{{ .Name }}Heap);
	perftest::RegisterTest("LLCPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
	perftest::RegisterTest("LLCPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
	{{ if .EnableSendEventBenchmark }}
	perftest::RegisterTest("LLCPP/SendEvent/{{ .Path }}/Steps", BenchmarkSendEvent{{ .Name }});
	{{- end -}}
	perftest::RegisterTest("Memcpy/{{ .Path }}", BenchmarkMemcpy{{ .Name }});
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmarkTmplInput struct {
	Path, Name, Type, EventProtocolType string
	ValueBuildHeap, ValueVarHeap        string
	EnableSendEventBenchmark            bool
}

// Generate generates Low-Level C++ benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root) (map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	files := map[string][]byte{}
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valBuildHeap, valVarHeap := libllcpp.BuildValueHeap(gidlBenchmark.Value, decl)
		var buf bytes.Buffer
		if err := benchmarkTmpl.Execute(&buf, benchmarkTmplInput{
			Path:                     gidlBenchmark.Name,
			Name:                     benchmarkName(gidlBenchmark.Name),
			Type:                     benchmarkTypeFromValue(gidlBenchmark.Value),
			EventProtocolType:        benchmarkTypeFromValue(gidlBenchmark.Value) + "EventProtocol",
			ValueBuildHeap:           valBuildHeap,
			ValueVarHeap:             valVarHeap,
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
		}); err != nil {
			return nil, err
		}
		files[benchmarkName("_"+gidlBenchmark.Name)] = buf.Bytes()
	}
	return files, nil
}

func benchmarkTypeFromValue(value gidlir.Value) string {
	return fmt.Sprintf("llcpp::benchmarkfidl::%s", gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
