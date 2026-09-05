// Called only for an already-published release. Never creates or replaces one.
module.exports = async ({github, context}, readFile = require('node:fs/promises').readFile) => {
  const release = context.payload.release;
  if (context.eventName !== 'release' || context.payload.action !== 'published' ||
      !release?.id || context.ref !== `refs/tags/${release.tag_name}`) {
    throw new Error('Expected an existing published release and matching tag');
  }
  const name = 'GBARunner3.zip';
  const assets = await github.paginate(github.rest.repos.listReleaseAssets, {
    ...context.repo, release_id: release.id, per_page: 100,
  });
  if (assets.some(asset => asset.name === name)) {
    throw new Error('Refusing to overwrite an existing release asset');
  }
  const data = await readFile(name);
  await github.rest.repos.uploadReleaseAsset({
    ...context.repo, release_id: release.id, name, data,
    headers: {'content-type': 'application/zip', 'content-length': data.length},
  });
};
