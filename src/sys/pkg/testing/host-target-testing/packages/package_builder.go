// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
)

type PackageBuilder struct {
	Name     string
	Version  string
	Cache    string
	Contents map[string]string
}

func parsePackageJSON(path string) (string, string, error) {
	jsonData, err := ioutil.ReadFile(path)
	if err != nil {
		return "", "", fmt.Errorf("failed to read file at %s. %w", path, err)
	}
	var packageInfo pkg.Package
	if err := json.Unmarshal(jsonData, &packageInfo); err != nil {
		return "", "", fmt.Errorf("failed to unmarshal json data. %w", err)
	}
	return packageInfo.Name, packageInfo.Version, nil
}

// NewPackage returns a PackageBuilder
// Must call `Close()` to clean up PackageBuilder
func NewPackageBuilder() *PackageBuilder {
	// Create temporary directory to store any additions that come in.
	tempDir, err := ioutil.TempDir("", "pm-temp-resource")
	if err != nil {
		log.Fatalf("Failed to create temp directory. %s", err)
	}
	return &PackageBuilder{
		Name:     "",
		Version:  "",
		Cache:    tempDir,
		Contents: make(map[string]string)}
}

// NewPackageFromDir returns a PackageBuilder that initializes from the `dir` package directory.
// Must call `Close()` to clean up PackageBuilder
func NewPackageBuilderFromDir(dir string) (*PackageBuilder, error) {
	pkg := NewPackageBuilder()

	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return fmt.Errorf("walk of %s failed. %w", dir, err)
		}
		if !info.IsDir() {
			relativePath := strings.Replace(path, dir+"/", "", 1)
			pkg.Contents[relativePath] = path

			if strings.HasSuffix(path, "meta/package") {
				// Grab the package name and version from the package JSON.
				name, version, err := parsePackageJSON(path)
				if err != nil {
					return fmt.Errorf("failed to parse package manifest. %w", err)
				}
				pkg.Name = name
				pkg.Version = version
			}
		}
		return nil
	})
	if err != nil {
		return nil, fmt.Errorf("error when walking the directory. %w", err)
	}
	if pkg.Name == "" || pkg.Version == "" {
		return nil, fmt.Errorf("missing package info and version information.")
	}

	return pkg, nil
}

// Close removes temporary directories created by PackageBuilder.
func (p *PackageBuilder) Close() {
	os.RemoveAll(p.Cache)
}

// Add a resource to the package at the given path.
func (p *PackageBuilder) AddResource(path string, contents io.Reader) error {
	if _, ok := p.Contents[path]; ok {
		return fmt.Errorf("a resource already exists at path %s", path)
	}
	data, err := ioutil.ReadAll(contents)
	if err != nil {
		return fmt.Errorf("failed to read file. %w", err)
	}
	tempPath := filepath.Join(p.Cache, path)
	if err := os.MkdirAll(filepath.Dir(tempPath), os.ModePerm); err != nil {
		return fmt.Errorf("failed to create parent directories for %s. %w", tempPath, err)
	}
	if err = ioutil.WriteFile(tempPath, data, 0644); err != nil {
		return fmt.Errorf("failed to write data to %s. %w", tempPath, err)
	}
	p.Contents[path] = tempPath
	return nil
}

func tempConfig(dir string, name string, version string) (*build.Config, error) {
	cfg := &build.Config{
		OutputDir:    filepath.Join(dir, "output"),
		ManifestPath: filepath.Join(dir, "manifest"),
		KeyPath:      filepath.Join(dir, "key"),
		TempDir:      filepath.Join(dir, "tmp"),
		PkgName:      name,
		PkgVersion:   version,
	}
	for _, d := range []string{cfg.OutputDir, cfg.TempDir} {
		os.MkdirAll(d, os.ModePerm)
	}
	return cfg, nil
}

// Publish the package to the repository.
func (p *PackageBuilder) Publish(pkgRepo *Repository) error {
	// Open repository
	// Repository.Dir contains a trailing `repository` in the path that we don't want.
	repoDir := path.Dir(pkgRepo.Dir)
	pmRepo, err := repo.New(repoDir)
	if err != nil {
		return fmt.Errorf("failed to open repository at %s. %w", pkgRepo.Dir, err)
	}
	// Create Config.
	dir, err := ioutil.TempDir("", "pm-temp-config")
	if err != nil {
		return fmt.Errorf("failed to create temp directory for the config")
	}
	defer os.RemoveAll(dir)

	cfg, err := tempConfig(dir, p.Name, p.Version)
	if err != nil {
		return fmt.Errorf("failed to create temp config to fill with our data. %w", err)
	}
	pack, err := cfg.Package()
	if err != nil {
		return fmt.Errorf("failed to create package for the given config. %w", err)
	}
	pkgPath := filepath.Join(filepath.Dir(cfg.ManifestPath), "package")
	if err := os.MkdirAll(filepath.Join(pkgPath, "meta"), os.ModePerm); err != nil {
		return fmt.Errorf("failed to make parent dirs for meta/package. %w", err)
	}
	pkgJSON := filepath.Join(pkgPath, "meta", "package")
	b, err := json.Marshal(&pack)
	if err != nil {
		return fmt.Errorf("failed to marshal package into JSON. %w", err)
	}
	if err := ioutil.WriteFile(pkgJSON, b, os.ModePerm); err != nil {
		return fmt.Errorf("failed to write JSON to package file. %w", err)
	}
	mfst, err := os.Create(cfg.ManifestPath)
	if err != nil {
		return fmt.Errorf("failed to create package manifest path. %w", err)
	}
	defer mfst.Close()

	if _, err := fmt.Fprintf(mfst, "meta/package=%s\n", pkgJSON); err != nil {
		return fmt.Errorf("failed to write package JSON to file. %w", err)
	}

	// Fill config with our contents.
	for relativePath, sourcePath := range p.Contents {
		if relativePath == "meta/contents" || relativePath == "meta/package" {
			continue
		}
		if _, err := fmt.Fprintf(mfst, "%s=%s\n", relativePath, sourcePath); err != nil {
			return fmt.Errorf("failed to record entry '%s' as '%s' into manifest. %w", p.Name, sourcePath, err)
		}
	}

	// Save changes to config.
	if err := build.Update(cfg); err != nil {
		return fmt.Errorf("failed to update config. %w", err)
	}
	if _, err := build.Seal(cfg); err != nil {
		return fmt.Errorf("failed to seal config. %w", err)
	}

	outputManifest, err := cfg.OutputManifest()
	if err != nil {
		return fmt.Errorf("failed to output manifest. %w", err)
	}

	outputManifestPath := path.Join(cfg.OutputDir, "package_manifest.json")

	content, err := json.Marshal(outputManifest)
	if err != nil {
		return fmt.Errorf("failed to convert manifest to JSON. %w", err)
	}
	if err := ioutil.WriteFile(outputManifestPath, content, os.ModePerm); err != nil {
		return fmt.Errorf("failed to write manifest JSON to %s. %w", outputManifestPath, err)
	}
	defer os.RemoveAll(filepath.Dir(cfg.OutputDir))

	// Publish new config to repo.
	_, err = pmRepo.PublishManifest(outputManifestPath)
	if err != nil {
		return fmt.Errorf("failed to publish manifest. %w", err)
	}
	if err = pmRepo.CommitUpdates(true); err != nil {
		return fmt.Errorf("failed to commit updates to repo. %w", err)
	}
	return nil
}
