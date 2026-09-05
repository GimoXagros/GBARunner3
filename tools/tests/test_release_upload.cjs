// Entirely mocked: no GitHub API, token, release or tag is touched by this test.
const assert = require('node:assert/strict');
const upload = require('../../.github/scripts/upload-release.cjs');
(async () => {
  const context = {eventName: 'release', ref: 'refs/tags/synthetic', repo: {owner: 'test', repo: 'test'},
    payload: {action: 'published', release: {id: 123, tag_name: 'synthetic'}}};
  let calls = [];
  const github = {paginate: async () => [], rest: {repos: {listReleaseAssets() {},
    uploadReleaseAsset: async value => calls.push(value)}}};
  const read = async name => { assert.equal(name, 'GBARunner3.zip'); return Buffer.from('synthetic'); };
  await upload({github, context}, read);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].release_id, 123);
  assert.equal(calls[0].name, 'GBARunner3.zip');
  github.paginate = async () => [{name: 'GBARunner3.zip'}];
  await assert.rejects(upload({github, context}, read), /overwrite/);
  await assert.rejects(upload({github, context: {...context, eventName: 'push'}}, read), /published/);
  await assert.rejects(upload({github, context: {...context, ref: 'refs/tags/wrong'}}, read), /matching/);
  assert.equal(calls.length, 1);
  console.log('PASS: existing-release upload and overwrite/event/tag guards (mocked)');
})();
