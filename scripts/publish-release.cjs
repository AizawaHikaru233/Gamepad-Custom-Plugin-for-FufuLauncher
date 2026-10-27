const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');

// Called only by the default-branch workflow_dispatch job with GITHUB_TOKEN.
// Keep all mutations after artifact validation and existing-release checks.
module.exports = async function publishRelease({ github, context, core, directory = 'dist', version = process.env.RELEASE_VERSION }) {
  if (context.eventName !== 'workflow_dispatch' ||
      context.ref !== `refs/heads/${context.payload.repository.default_branch}`) {
    throw new Error('Publication requires a manual run on the default branch.');
  }
  if (typeof version !== 'string' || version.trim() !== version ||
      !/^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/.test(version) ||
      version.split('.').some(part => Number(part) > 65535)) throw new Error('Invalid release version.');
  const { owner, repo } = context.repo;
  const tag = `v${version}`;
  const name = `GamepadCustomPlugin-${version}.zip`;
  const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
  const readJson = file => JSON.parse(fs.readFileSync(path.join(directory, file), 'utf8').replace(/^\uFEFF/, ''));
  const hashes = readJson('package-hashes.json');
  const metadata = readJson('metadata.json');
  if (hashes.id !== 'XboxGamepadPlugin' || metadata.id !== hashes.id || hashes.version !== version ||
      metadata.version !== version || metadata.dll_file_name !== 'GamepadCustomPlugin.dll' ||
      hashes.archive_file_name !== name || metadata.archive_file_name !== name) throw new Error('Release artifact identity differs.');
  for (const [file, hash] of [[name, hashes.archive_sha256], ['metadata.json', hashes.metadata_sha256],
    ['install.lua', hashes.installer_sha256], ['compression.json', hashes.compression_sha256]]) {
    if (!/^[a-f0-9]{64}$/.test(hash) || sha256(fs.readFileSync(path.join(directory, file))) !== hash) {
      throw new Error(`Artifact SHA256 mismatch: ${file}`);
    }
  }
  const zip = fs.readFileSync(path.join(directory, name));

  // List all pages: tag lookup alone must not miss an unfinished draft.
  const releases = await github.paginate(github.rest.repos.listReleases, { owner, repo, per_page: 100 });
  const matches = releases.filter(release => release.tag_name === tag);
  if (matches.some(release => release.draft)) throw new Error(`Unfinished draft exists for ${tag}; finish or remove it manually.`);
  if (matches.length) { core.info(`${tag} is already public; skipping without changing its assets.`); return 'skipped'; }

  let ref;
  try { ref = (await github.rest.git.getRef({ owner, repo, ref: `tags/${tag}` })).data; }
  catch (error) { if (error.status !== 404) throw error; }
  if (ref) {
    let object = ref.object;
    for (let depth = 0; object.type === 'tag' && depth < 10; depth++) {
      object = (await github.rest.git.getTag({ owner, repo, tag_sha: object.sha })).data.object;
    }
    if (object.type !== 'commit' || object.sha !== context.sha) throw new Error(`Existing tag ${tag} points to a different commit.`);
  } else {
    await github.rest.git.createRef({ owner, repo, ref: `refs/tags/${tag}`, sha: context.sha });
  }
  const draft = (await github.rest.repos.createRelease({ owner, repo, tag_name: tag,
    target_commitish: context.sha, name: `GamepadCustomPlugin ${version}`, draft: true,
    generate_release_notes: true })).data;
  // Upload exactly one file. Any failure leaves a draft for the maintainer to inspect.
  const asset = (await github.rest.repos.uploadReleaseAsset({ owner, repo, release_id: draft.id,
    name, data: zip, headers: { 'content-type': 'application/zip', 'content-length': zip.length } })).data;
  if (asset.name !== name || asset.state !== 'uploaded' || asset.size !== zip.length ||
      asset.digest !== `sha256:${hashes.archive_sha256}`) throw new Error('Uploaded ZIP identity, size or SHA256 differs; draft retained.');
  const assets = await github.paginate(github.rest.repos.listReleaseAssets, { owner, repo, release_id: draft.id, per_page: 100 });
  if (assets.length !== 1 || assets[0].id !== asset.id) throw new Error('Unexpected draft attachments; draft retained.');
  // Recheck the tag immediately before publication; never move an existing tag.
  const current = (await github.rest.git.getRef({ owner, repo, ref: `tags/${tag}` })).data.object;
  let target = current;
  for (let depth = 0; target.type === 'tag' && depth < 10; depth++) {
    target = (await github.rest.git.getTag({ owner, repo, tag_sha: target.sha })).data.object;
  }
  if (target.type !== 'commit' || target.sha !== context.sha) throw new Error('Release tag changed; draft retained.');
  await github.rest.repos.updateRelease({ owner, repo, release_id: draft.id, draft: false });
  core.info(`Published ${tag} from ${context.sha}, with ${name} as the only attachment.`);
  return 'published';
};
