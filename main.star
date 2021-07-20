#!/usr/bin/env lucicfg

luci.project(
    name = "skia-skcms",
    buildbucket = "cr-buildbucket.appspot.com",
    swarming = "chromium-swarm.appspot.com",
    acls = [
        acl.entry(acl.PROJECT_CONFIGS_READER, groups = [ "all" ]),
        acl.entry(acl.CQ_COMMITTER, groups = [ "project-skia-committers" ]),
        acl.entry(acl.CQ_DRY_RUNNER, groups = [ "project-skia-tryjob-access" ]),
    ],
    logdog = "luci-logdog",
)

luci.logdog(
    gs_bucket = "skia-logdog",
)
