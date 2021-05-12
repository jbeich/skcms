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

luci.cq (
    status_host = "chromium-cq-status.appspot.com",
    submit_burst_delay = time.duration(300000),
    submit_max_burst = 2,
)

luci.cq_group(
    name = "skcms",
    watch = cq.refset(
        repo = "https://skia.googlesource.com/skcms",
        refs = [ "refs/heads/.+" ],
    ),
    retry_config = cq.retry_config(
        single_quota = 1,
        global_quota = 2,
        failure_weight = 2,
        transient_failure_weight = 1,
        timeout_weight = 2,
    ),
    verifiers = [
        luci.cq_tryjob_verifier(
            builder = "skia:skia.primary/skcms",
        ),
    ],
)
