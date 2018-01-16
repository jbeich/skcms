// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"go.skia.org/infra/task_scheduler/go/specs"
)

var (
	TASKS = []string{
		"Linux",
		"Mac",
	}
)

func addTask(b *specs.TasksCfgBuilder, task string) {
	dimensions := map[string][]string{
		"Linux": []string{"cpu:x86-64-Haswell_GCE", "os:Debian-9.2"},
		"Mac":   []string{"cpu:x86-64-E5-2697_v2", "os:Mac-10.13.2"},
	}

	b.MustAddTask(task, &specs.TaskSpec{
		Command: []string{
			"python", "bot.py",
		},
		Dimensions:  append(dimensions[task], "gpu:none", "pool:Skia"),
		Isolate:     "bot.isolate",
		Priority:    0.8,
		MaxAttempts: 1,
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
	b.MustAddJob("All", &specs.JobSpec{
		Priority:  0.8,
		TaskSpecs: TASKS,
	})
	b.MustFinish()
}
