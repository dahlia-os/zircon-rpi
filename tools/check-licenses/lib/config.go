// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"encoding/json"
	"io/ioutil"
	"os"
)

// Config values are populated from the the json file at the default or user-specified path
type Config struct {
	FilesRegex          []string `json:"filesRegex,omitempty"`
	SkipDirs            []string `json:"skipDirs"`
	SkipFiles           []string `json:"skipFiles"`
	TextExtensionList   []string `json:"textExtensionList"`
	MaxReadSize         int      `json:"maxReadSize"`
	SeparatorWidth      int      `json:"separatorWidth"`
	OutputFilePrefix    string   `json:"outputFilePrefix"`
	OutputFileExtension string   `json:"outputFileExtension"`
	Product             string   `json:"product"`
	SingleLicenseFiles  []string `json:"singleLicenseFiles"`
	LicensePatternDir   string   `json:"licensePatternDir"`
	BaseDir             string   `json:"baseDir"`
	Target              string   `json:"target"`
	LogLevel            string   `json:"logLevel"`
	TextExtensions      map[string]bool
}

// Init populates Config object with values found in the json config file
func (config *Config) Init(configJson *string) error {
	jsonFile, err := os.Open(*configJson)
	defer jsonFile.Close()
	if err != nil {
		return err
	}
	byteValue, err := ioutil.ReadAll(jsonFile)
	if err != nil {
		return err
	}
	if err = json.Unmarshal(byteValue, &config); err != nil {
		return err
	}
	config.createTextExtensions()
	return nil
}

func (config *Config) createTextExtensions() {
	config.TextExtensions = make(map[string]bool)
	for _, item := range config.TextExtensionList {
		config.TextExtensions[item] = true
	}
}
