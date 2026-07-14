#!/bin/bash

set -eux -o pipefail

mkdir -p public/coverage

GCOVR_COMMON_ARGS=(
    _build
    --root ./
    --txt-metric branch
    --merge-mode-functions=merge-use-line-0
    --exclude '_build/.*'
    --exclude-lines-by-pattern '.*g_(assert_not|warn_if|return_if|return_val_if)_reached.*'
)

gcovr "${GCOVR_COMMON_ARGS[@]}" --html-details public/coverage/index.html

# Generate the cobertura XML format for GitLab's line-by-line coverage report in MR diffs.
#
# However, guard it for not being over 50 MB in size; we had a case before where it was over
# 500 MB and that OOM'd gitlab's redis.

gcovr "${GCOVR_COMMON_ARGS[@]}" --cobertura coverage.xml

# gcovr writes an absolute path into <source> (e.g. /builds/mcrha/evolution-data-server).
# For fork MRs the fork namespace differs from the target project namespace, so GitLab
# cannot match the path and diff annotations silently fail. Use "." to let GitLab resolve
# the repo-relative filenames directly.
sed -i 's|<source>[^<]*</source>|<source>.</source>|' coverage.xml

size=$(wc -c < coverage.xml)
if [ "$size" -ge 52428800 ]
then
    rm coverage.xml
    echo "coverage.xml is over 50 MB, removing it so it will not be used"
fi

# Print "Coverage: 42.42" so .gitlab-ci.yml will pick it up with a regex
#
# We scrape this from the HTML report, not the JSON summary, because coverage.json
# uses no decimal places, just something like "42%".

grep -Eo '<td class="coverage-[a-z]+">[0-9.]+%</td>' public/coverage/index.html | head -n 1 | grep -Eo '[0-9.]+' | awk '{ print "Coverage:", $1 }'
