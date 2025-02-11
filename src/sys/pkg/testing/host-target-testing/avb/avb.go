// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package avb

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
)

type AVBTool struct {
	avbToolPath     string
	keyPath         string
	keyMetadataPath string
	stdout          io.Writer
}

func NewAVBTool(avbToolPath string, keyPath string, keyMetadataPath string) (*AVBTool, error) {
	return newAVBToolWithStdout(avbToolPath, keyPath, keyMetadataPath, nil)
}

func newAVBToolWithStdout(avbToolPath string, keyPath string, keyMetadataPath string, stdout io.Writer) (*AVBTool, error) {
	if _, err := os.Stat(keyPath); err != nil {
		return nil, err
	}
	if _, err := os.Stat(keyMetadataPath); err != nil {
		return nil, err
	}
	return &AVBTool{
		avbToolPath:     avbToolPath,
		keyPath:         keyPath,
		keyMetadataPath: keyMetadataPath,
		stdout:          stdout,
	}, nil
}

func (a *AVBTool) MakeVBMetaImage(ctx context.Context, destPath string, srcPath string, props map[string]string) error {
	path, err := exec.LookPath(a.avbToolPath)
	if err != nil {
		return err
	}

	args := []string{
		"make_vbmeta_image",
		"--output", destPath,
		"--key", a.keyPath,
		"--algorithm", "SHA512_RSA4096",
		"--public_key_metadata", a.keyMetadataPath,
		"--include_descriptors_from_image", srcPath,
	}

	for key, value := range props {
		propertyFile, err := ioutil.TempFile("", "")
		if err != nil {
			return err
		}
		defer os.Remove(propertyFile.Name())

		if _, err := propertyFile.WriteString(value); err != nil {
			return err
		}

		if err := propertyFile.Close(); err != nil {
			return err
		}

		args = append(args, "--prop_from_file", fmt.Sprintf("%s:%s", key, propertyFile.Name()))
	}

	log.Printf("running: %s %q", path, args)
	cmd := exec.CommandContext(ctx, path, args...)
	if a.stdout != nil {
		cmd.Stdout = a.stdout
	} else {
		cmd.Stdout = os.Stdout
	}
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
