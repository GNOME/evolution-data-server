#!/bin/bash

set -eux -o pipefail

mkdir -p public

call_grcov() {
    output_type=$1
    output_path=$2

    grcov _build                                                                \
          --source-dir ./                                                       \
          --prefix-dir ../                                                      \
          --output-type "$output_type"                                          \
          --branch                                                              \
          --ignore-not-existing                                                 \
          --ignore '_build/*'                                                   \
          --excl-line 'g_(assert_not|warn_if|return_if|return_val_if)_reached'  \
          -o "$output_path"
}

call_grcov html public/coverage

# Generate the cobertura XML format for GitLab's line-by-line coverage report in MR diffs.
#
# However, guard it for not being over 50 MB in size; we had a case before where it was over
# 500 MB and that OOM'd gitlab's redis.

call_grcov cobertura coverage.xml
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

grep -Eo 'abbr title.* %' public/coverage/index.html | head -n 1 | grep -Eo '[0-9.]+ %' | grep -Eo '[0-9.]+' | awk '{ print "Coverage:", $1 }'
