// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

/*
	Generate the tasks.json file.
*/

import (
	"strings"

	"go.skia.org/infra/task_scheduler/go/specs"
)

var (
	// "Constants"

	// Top-level list of all Jobs to run at each commit.
	JOBS = []string{
		"Dummy-Tests",
	}
)

// Dimensions for Linux GCE instances.
func linuxGceDimensions() []string {
	return []string{
		"pool:Skia",
		"os:Debian-9.2",
		"gpu:none",
		"cpu:x86-64-Haswell_GCE",
	}
}

// dummy creates a simple dummy task. Returns the name of the last Task in the
// generated chain of Tasks, which the Job should add as a dependency.
func dummy(b *specs.TasksCfgBuilder, name string) string {
	task := &specs.TaskSpec{
		Command: []string{
			"/bin/echo", "hello world",
		},
		Dimensions:  linuxGceDimensions(),
		Isolate:     "dummy.isolate",
		Priority:    0.8,
		MaxAttempts: 1,
	}
	b.MustAddTask(name, task)
	return name
}

// process generates Tasks and Jobs for the given Job name.
func process(b *specs.TasksCfgBuilder, name string) {
	deps := []string{}

	// Dummy task.
	if strings.Contains(name, "Dummy") {
		deps = append(deps, dummy(b, name))
	}

	// Add the Job spec.
	b.MustAddJob(name, &specs.JobSpec{
		Priority:  0.8,
		TaskSpecs: deps,
	})
}

// Regenerate the tasks.json file.
func main() {
	b := specs.MustNewTasksCfgBuilder()

	// Create Tasks and Jobs.
	for _, name := range JOBS {
		process(b, name)
	}

	b.MustFinish()
}
