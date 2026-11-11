#!/usr/bin/env bash
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# Builds the CI image from Tools/CI/Dockerfile on the Gitea server and pushes it to Gitea's container registry.
# Run it from the desktop, BEFORE pushing a commit that names a new image tag in .gitea/workflows/linux.yml.
#
#   Tools/CI/publish-image.sh             build, push, check
#   Tools/CI/publish-image.sh --dry-run   only print what would run
#
# The image name and tag are read from linux.yml, so the two can't differ.
# The build and the push happen on the server because the public hostname limits an upload to 100 MB (README.md,
# "The build image"): the push goes to the registry through localhost there.
#
#   PRIMAL_CI_HOST            ssh name of the Gitea server (default: dev)
#   PRIMAL_CI_REGISTRY_PORT   port Gitea is published on, on that server (default: 3000)
#
# The first push asks for a login: your Gitea user name, and as password an access token with the write:package
# scope (Gitea: Settings > Applications > Generate New Token).
set -euo pipefail

cd "$(dirname "$0")/../.."

readonly host=${PRIMAL_CI_HOST:-dev}
readonly port=${PRIMAL_CI_REGISTRY_PORT:-3000}
dry_run=false
if [[ ${1:-} == --dry-run ]]; then
    dry_run=true
elif [[ $# -gt 0 ]]; then
    echo "usage: $0 [--dry-run]" >&2
    exit 2
fi

# Every job in linux.yml must name the same image.
mapfile -t images < <(sed -nE 's/^[[:space:]]*image:[[:space:]]*([^[:space:]]+).*$/\1/p' .gitea/workflows/linux.yml | sort -u)
if [[ ${#images[@]} -ne 1 ]]; then
    echo "linux.yml names ${#images[@]} different images, expected 1:" >&2
    printf '  %s\n' "${images[@]}" >&2
    exit 1
fi
readonly image=${images[0]}
# gitea.example.com/owner/name:tag -> localhost:<port>/owner/name:tag
readonly local_image="localhost:${port}/${image#*/}"

run() {
    printf '+ %s\n' "$*"
    if ! ${dry_run}; then
        "$@"
    fi
}

echo "Image:  ${image}"
echo "Server: ${host} (registry at localhost:${port})"
echo

# The Dockerfile is sent on stdin: the build needs no other file.
printf '+ ssh %s docker build -t %s - < Tools/CI/Dockerfile\n' "${host}" "${image}"
if ! ${dry_run}; then
    ssh "${host}" docker build -t "${image}" - < Tools/CI/Dockerfile
fi

run ssh "${host}" docker tag "${image}" "${local_image}"

if ! run ssh "${host}" docker push "${local_image}"; then
    echo
    echo "The push failed. Logging in to the registry (Gitea user name; password: a token with write:package)."
    run ssh -t "${host}" docker login "localhost:${port}"
    run ssh "${host}" docker push "${local_image}"
fi

# Only the extra name goes; the image stays on the server under its public name, where the runner uses it.
run ssh "${host}" docker rmi "${local_image}"

echo
if ${dry_run}; then
    echo "Dry run: nothing was built or pushed."
    exit 0
fi

# The runner pulls through the public name. Check that it is there.
if docker manifest inspect "${image}" > /dev/null 2>&1; then
    echo "Published: ${image} can be pulled."
else
    echo "Pushed, but this machine can't see ${image} without a login."
    echo "That is fine if the jobs pull with credentials; otherwise make the package visible (README.md)."
fi
echo "Next: the Gitea steps in Tools/CI/README.md, \"After publishing an image\"."
