#!/usr/bin/env bash
set -euo pipefail
umask 077
cd "$(dirname "$0")/../.."

container=xiaozhi-dev
revision=$(docker exec -u dev -w /workspace "$container" git rev-parse "${1:-HEAD}^{commit}")
tag="${revision:0:12}"
prepared="/opt/xiaozhi-dev/run/release-$tag"
test "$(docker exec -u dev "$container" cat "$prepared/revision")" = "$revision"
runtime=${XIAOZHI_RUNTIME_DIR:-/home/timyai/dev/runtime/xiaozhi}
mkdir -p "$runtime/packages"
release="$runtime/packages/$tag"
test ! -e "$release"
contexts=$(mktemp -d "$runtime/packages/.contexts.XXXXXX")
trap 'rm -rf -- "$contexts"' EXIT
mkdir -m 700 "$release"

for part in api web voice; do
  docker cp "$container:$prepared/$part" "$contexts/$part"
  docker exec -u dev -w /workspace "$container" git show \
    "$revision:deploy/local-zhou/release-images/$part.Dockerfile" > "$contexts/$part.Dockerfile"
done
docker exec -u dev -w /workspace "$container" git show \
  "$revision:deploy/production/xiaozhi/web.conf" > "$contexts/web/web.conf"
for part in api web voice; do
  docker build --platform linux/amd64 --provenance=false \
    --build-arg "SOURCE_REVISION=$revision" \
    --file "$contexts/$part.Dockerfile" \
    --tag "xiaozhi-release-$part:$tag" "$contexts/$part"
done

base_images=(
  'mysql:8.0.41@sha256:bf577825b52ab281d6281fb281eabbfdc73507eda8f2c2745790251533ef0306 xiaozhi-release-mysql:8.0.41'
  'redis:8.0.0-alpine@sha256:0779069b3c24a47a2f681855c1c01d046793e7c5f7d2b079c2aa0652c42eaf0e xiaozhi-release-redis:8.0.0'
  'nginx:1.27.4-alpine@sha256:4ff102c5d78d254a6f0da062b3cf39eaf07f01eec0927fd21e219d0af8bc0591 xiaozhi-release-nginx:1.27.4'
)
images=("xiaozhi-release-api:$tag" "xiaozhi-release-web:$tag" "xiaozhi-release-voice:$tag")
for pair in "${base_images[@]}"; do
  read -r source target <<< "$pair"
  if ! docker image inspect "$source" >/dev/null 2>&1; then
    docker pull --platform linux/amd64 "$source"
  fi
  docker tag "$source" "$target"
  images+=("$target")
done

{
  printf 'source %s\nplatform linux/amd64\n' "$revision"
  for image in "${images[@]}"; do
    docker image inspect --format "$image {{.Architecture}} {{.Id}} {{json .RootFS.Layers}}" "$image"
  done
} > "$release/images.manifest"
docker image save --platform linux/amd64 "${images[@]}" -o "$release/images.tar"
docker exec -u dev -w /workspace "$container" git archive --format=tar.gz \
  "$revision:deploy/production" > "$release/deployment.tar.gz"
(
  cd "$release"
  sha256sum images.tar images.manifest deployment.tar.gz > SHA256SUMS
)
printf '%s\n' "$release"
