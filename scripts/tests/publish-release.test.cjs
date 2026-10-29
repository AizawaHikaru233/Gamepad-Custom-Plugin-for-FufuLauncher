const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createHash } = require('node:crypto');
const publish = require('../publish-release.cjs');

function fixture(t, options = {}) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gamepad-publish-test-'));
  // Only named files created by this test are removed; no recursive deletion.
  t.after(() => { for (const file of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, file)); fs.rmdirSync(directory); });
  const version = '1.2.3';
  const tag = `v${version}`;
  const name = `GamepadCustomPlugin-${version}.zip`;
  const sha = 'a'.repeat(40);
  const hashes = { id: 'XboxGamepadPlugin', version, archive_file_name: name };
  const files = {
    [name]: Buffer.from('mock ZIP: this suite tests API ordering, not actual packaging'),
    'metadata.json': JSON.stringify({ id: hashes.id, version, dll_file_name: 'GamepadCustomPlugin.dll', archive_file_name: name }),
    'install.lua': 'install.download_plugin("XboxGamepadPlugin")',
    'compression.json': '{}'
  };
  const fields = ['archive_sha256', 'metadata_sha256', 'installer_sha256', 'compression_sha256'];
  Object.entries(files).forEach(([file, bytes], i) => {
    fs.writeFileSync(path.join(directory, file), bytes);
    hashes[fields[i]] = createHash('sha256').update(bytes).digest('hex');
  });
  fs.writeFileSync(path.join(directory, 'package-hashes.json'), JSON.stringify(hashes));
  const calls = [];
  let hasTag = options.tagSha !== undefined;
  const asset = { id: 2, name, size: files[name].length, state: 'uploaded', digest: `sha256:${hashes.archive_sha256}` };
  const repos = {
    listReleases: async () => {},
    listReleaseAssets: async () => {},
    createRelease: async args => { calls.push(['draft', args]); return { data: { id: 1 } }; },
    uploadReleaseAsset: async args => {
      calls.push(['upload', args]);
      if (options.uploadFailure) throw new Error('upload failed');
      return { data: { ...asset, ...(options.asset || {}) } };
    },
    updateRelease: async args => { calls.push(['publish', args]); return { data: {} }; }
  };
  const git = {
    getRef: async () => {
      calls.push(['lookup-tag']);
      if (!hasTag) throw Object.assign(new Error('not found'), { status: options.lookupStatus || 404 });
      return { data: { object: { type: options.annotated ? 'tag' : 'commit', sha: options.tagSha || sha } } };
    },
    getTag: async () => ({ data: { object: { type: 'commit', sha: options.targetSha || sha } } }),
    createRef: async args => { calls.push(['tag', args]); hasTag = true; return { data: {} }; }
  };
  const github = { rest: { repos, git }, paginate: async (method) => {
    if (options.apiFailure) throw new Error('API unavailable');
    if (method === repos.listReleases) return options.releases || [];
    return options.extraAsset ? [asset, { id: 99 }] : [asset];
  } };
  const context = { eventName: 'workflow_dispatch', ref: 'refs/heads/master', sha,
    payload: { repository: { default_branch: 'master' } }, repo: { owner: 'owner', repo: 'repo' } };
  return { calls, directory, name, sha, tag, context,
    run: overrides => publish({ github, context, core: { info() {} }, directory, version, ...overrides }) };
}

test('new version: tag exact commit, draft, ZIP only, then publish', async t => {
  const f = fixture(t);
  assert.equal(await f.run(), 'published');
  assert.deepEqual(f.calls.map(c => c[0]), ['lookup-tag', 'tag', 'draft', 'upload', 'lookup-tag', 'publish']);
  assert.deepEqual(f.calls.find(c => c[0] === 'tag')[1], { owner: 'owner', repo: 'repo', ref: `refs/tags/${f.tag}`, sha: f.sha });
  const draft = f.calls.find(c => c[0] === 'draft')[1];
  assert.equal(draft.draft, true);
  assert.equal(draft.name, 'GamepadCustomPlugin 1.2.3');
  assert.equal(draft.target_commitish, f.sha);
  assert.equal(f.calls.find(c => c[0] === 'upload')[1].name, f.name);
  assert.equal(f.calls.at(-1)[1].draft, false);
});
test('published version skips without modifying attachments or tags', async t => {
  const f = fixture(t, { releases: [{ tag_name: 'v1.2.3', draft: false }] });
  assert.equal(await f.run(), 'skipped');
  assert.deepEqual(f.calls, []);
});
test('unfinished draft blocks even when another matching release is public', async t => {
  const f = fixture(t, { releases: [{ tag_name: 'v1.2.3', draft: false }, { tag_name: 'v1.2.3', draft: true }] });
  await assert.rejects(f.run(), /Unfinished draft/);
  assert.deepEqual(f.calls, []);
});
for (const [label, options, pattern] of [
  ['API failure', { apiFailure: true }, /API unavailable/],
  ['tag lookup forbidden', { lookupStatus: 403 }, /not found/],
  ['upload failure', { uploadFailure: true }, /upload failed/],
  ['uploaded hash mismatch', { asset: { digest: 'sha256:bad' } }, /SHA256 differs/],
  ['unexpected attachments', { extraAsset: true }, /Unexpected draft attachments/]
]) {
  test(`${label} never publishes`, async t => {
    const f = fixture(t, options);
    await assert.rejects(f.run(), pattern);
    assert.ok(!f.calls.some(c => c[0] === 'publish'));
  });
}
for (const annotated of [false, true]) {
  test(`existing ${annotated ? 'annotated' : 'lightweight'} tag pointing at an older commit is reused`, async t => {
    const f = fixture(t, { tagSha: 'c'.repeat(40), targetSha: 'b'.repeat(40), annotated });
    assert.equal(await f.run(), 'published');
    assert.ok(!f.calls.some(c => c[0] === 'tag'));
    assert.equal(f.calls.find(c => c[0] === 'draft')[1].target_commitish, f.sha);
  });
}
test('artifact corruption fails before any GitHub mutation', async t => {
  const f = fixture(t);
  fs.appendFileSync(path.join(f.directory, f.name), 'corruption');
  await assert.rejects(f.run(), /SHA256 mismatch/);
  assert.deepEqual(f.calls, []);
});
test('PRs, pushes and manual runs outside default branch cannot publish', async t => {
  const f = fixture(t);
  for (const [eventName, ref] of [['pull_request', 'refs/heads/master'], ['push', 'refs/heads/master'], ['workflow_dispatch', 'refs/heads/feature']]) {
    await assert.rejects(f.run({ context: { ...f.context, eventName, ref } }), /manual run on the default branch/);
  }
  assert.deepEqual(f.calls, []);
});
test('only canonical three-part PE-compatible versions are accepted', async t => {
  const f = fixture(t);
  for (const version of ['', 'v1.2.3', '1.2', '1.2.3.4', '1.2.3\n', ' 1.2.3', '1.2.3-beta', '01.2.3', '65536.2.3', '１.2.3']) {
    await assert.rejects(f.run({ version }), /Invalid release version/);
  }
  assert.deepEqual(f.calls, []);
});
