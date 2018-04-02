// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"go.skia.org/infra/task_scheduler/go/specs"
)

var (
	TASKS = []string{
		"skcms-Linux",
		"skcms-Mac",
		"skcms-Win",
	}
)

func addTask(b *specs.TasksCfgBuilder, task string) {
	dimensions := map[string][]string{
		// For the moment we'd rather not run bots on Skylakes, which support AVX-512.
		"skcms-Linux": []string{"os:Linux", "cpu:x86-64-Haswell_GCE"},
		"skcms-Mac":   []string{"os:Mac"},
		// We think there's something amiss building on Win7 or Win8 bots, so restrict to 2016.
		"skcms-Win": []string{"os:Windows-2016Server"},
	}
	packages := map[string][]*specs.CipdPackage{
		"skcms-Linux": []*specs.CipdPackage{
			&specs.CipdPackage{
				Name:    "infra/ninja/linux-amd64",
				Path:    "ninja",
				Version: "version:1.8.2",
			},
			&specs.CipdPackage{
				Name:    "skia/bots/android_ndk_linux",
				Path:    "ndk",
				Version: "version:10",
			},
			&specs.CipdPackage{
				Name:    "skia/bots/clang_linux",
				Path:    "clang_linux",
				Version: "version:10",
			},
		},
		"skcms-Mac": []*specs.CipdPackage{
			&specs.CipdPackage{
				Name:    "infra/ninja/mac-amd64",
				Path:    "ninja",
				Version: "version:1.8.2",
			},
			&specs.CipdPackage{
				Name:    "skia/bots/android_ndk_darwin",
				Path:    "ndk",
				Version: "version:4",
			},
		},
		"skcms-Win": []*specs.CipdPackage{
			&specs.CipdPackage{
				Name:    "skia/bots/win_ninja",
				Path:    "ninja",
				Version: "version:2",
			},
			&specs.CipdPackage{
				Name:    "skia/bots/win_toolchain",
				Path:    "t",
				Version: "version:7",
			},
			&specs.CipdPackage{
				Name:    "skia/bots/clang_win",
				Path:    "clang_win",
				Version: "version:5",
			},
		},
	}

	command := []string{"python", "skcms/infra/bots/bot.py"}
	for _, p := range packages[task] {
		command = append(command, p.Path)
	}

	b.MustAddTask(task, &specs.TaskSpec{
		CipdPackages: packages[task],
		Command:      command,
		Dimensions:   append(dimensions[task], "gpu:none", "pool:Skia"),
		Isolate:      "bot.isolate",
		Priority:     0.8,
		MaxAttempts:  1,
	})

	b.MustAddJob(task, &specs.JobSpec{
		Priority:  0.8,
		TaskSpecs: []string{task},
	})
}

func main() {
	b := specs.MustNewTasksCfgBuilder()
	for _, task := range TASKS {
		addTask(b, task)
	}

	b.MustAddJob("skcms", &specs.JobSpec{
		Priority:  0.8,
		TaskSpecs: TASKS,
	})
	b.MustFinish()
}
