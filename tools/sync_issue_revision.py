#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Revise existing specification issues without erasing implementation evidence.

This never creates issues or comments. Dry-run is the default and writes a
reviewable plan plus before/after bodies. --apply consumes that plan, rejects
concurrent body/title edits, and patches only title/body. Exact unchanged
criteria retain their checkboxes and inline notes; changed checked criteria or
notes are archived explicitly as superseded evidence. Unknown sections remain.

Examples:
  python3 tools/sync_issue_revision.py --dry-run
  python3 tools/sync_issue_revision.py --apply
  python3 tools/sync_issue_revision.py --self-test
"""
from __future__ import annotations

import argparse
import collections
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone

from publish_issues import REPO, render_issue_body
from spec import load_actions

ROOT = Path(__file__).resolve().parent.parent
SPEC_HEADINGS = ('PRD Action ID', '入口、作用对象与行为边界', '完成标准', '竞品对标出处')
CHECKBOX = re.compile(r'^- \[([ xX])\] (.*)$', re.MULTILINE)
ARCHIVE_HEADING = '## 规格修订前的验收记录'


def dump(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def read_json(path: Path):
    return json.loads(path.read_text(encoding='utf-8'))


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def fingerprint(value):
    payload = json.dumps(value, ensure_ascii=False, sort_keys=True).encode('utf-8')
    return hashlib.sha256(payload).hexdigest()


def body_identity(issue):
    return {'title': issue['title'], 'body': issue.get('body') or ''}


def untouched_identity(issue):
    return {'labels': sorted(x['name'] for x in issue.get('labels', [])),
            'state': issue['state'], 'state_reason': issue.get('state_reason'),
            'assignees': sorted(x['login'] for x in issue.get('assignees', []))}


def api(method, endpoint, value=None):
    # Structured stdin avoids shell interpolation, quoting and token disclosure.
    command = ['gh', 'api', '--method', method, endpoint,
               '-H', 'Accept: application/vnd.github+json']
    if value is not None:
        command += ['--input', '-']
    result = subprocess.run(command, input=None if value is None else json.dumps(value),
                            text=True, capture_output=True, timeout=90, cwd=ROOT)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'gh api failed')
    return json.loads(result.stdout) if result.stdout.strip() else None


def sections(body):
    """Return heading blocks while ignoring headings inside fenced evidence."""
    starts, offset, fence = [], 0, None
    for line in body.splitlines(keepends=True):
        marker = re.match(r'^\s{0,3}(`{3,}|~{3,})', line)
        if marker:
            token = marker[1]
            if fence is None:
                fence = (token[0], len(token))
            elif token[0] == fence[0] and len(token) >= fence[1]:
                fence = None
        elif fence is None:
            match = re.match(r'^## (.+?)\s*$', line)
            if match:
                starts.append((offset, offset + len(line), match[1]))
        offset += len(line)
    result = []
    for index, (start, content_start, heading) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(body)
        result.append({'heading': heading, 'start': start, 'end': end,
                       'content': body[content_start:end]})
    return result


def criterion_blocks(content):
    matches = list(CHECKBOX.finditer(content))
    blocks = collections.defaultdict(list)
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(content)
        block = content[match.start():end].rstrip()
        blocks[match[2]].append({'checked': match[1].lower() == 'x', 'block': block,
                              'notes': content[match.end():end].strip()})
    prefix = content[:matches[0].start()].strip() if matches else content.strip()
    return blocks, prefix


def render_revision(action, before_action, old_body):
    """Pure merge preserving evidence, exact checks and idempotent output."""
    generated = render_issue_body(action)
    wanted = {s['heading']: s['content'].strip() for s in sections(generated)}
    blocks = sections(old_body)
    by_heading = collections.defaultdict(list)
    for block in blocks:
        by_heading[block['heading']].append(block)
    expected_id = action['id']
    if len(by_heading['PRD Action ID']) == 1:
        actual_id = by_heading['PRD Action ID'][0]['content'].strip()
        if actual_id != expected_id:
            raise ValueError('body ACTION-ID mismatch: %s != %s' % (actual_id, expected_id))
    valid = all(len(by_heading[name]) == 1 for name in SPEC_HEADINGS)
    if not valid:
        # Unstructured legacy text cannot be safely dissected. Preserve all of
        # it visibly, with old checkboxes quoted so they are not live criteria.
        quoted = '\n'.join('> ' + line for line in old_body.splitlines())
        result = generated.rstrip() + '\n\n' + ARCHIVE_HEADING + '\n\n'
        result += '原正文结构无法无损分区；以下完整保留为历史记录，不代表新规格已验收。\n\n'
        result += quoted + '\n'
        return result, {'fallback_archive': True, 'preserved_checks': 0,
                        'archived_blocks': 1, 'new_unchecked': len(action['criteria'])}

    old_criteria, prefix = criterion_blocks(by_heading['完成标准'][0]['content'])
    preserved_checks = 0
    replacement_criteria, archives = [], []
    if prefix:
        # Keep a human-authored preamble without reinterpreting it as a rule.
        replacement_criteria.append(prefix)
    old_texts = set(old_criteria)
    for criterion in action['criteria']:
        candidates = old_criteria.get(criterion, [])
        if len(candidates) == 1:
            replacement_criteria.append(candidates[0]['block'])
            preserved_checks += int(candidates[0]['checked'])
        else:
            replacement_criteria.append('- [ ] ' + criterion)
    for criterion, candidates in old_criteria.items():
        if criterion in action['criteria'] and len(candidates) == 1:
            continue
        for candidate in candidates:
            if candidate['checked'] or candidate['notes'] or criterion not in before_action['criteria']:
                archives.append(candidate['block'])
    wanted['完成标准'] = '\n'.join(replacement_criteria)

    # Preserve nonstandard additions within generated entry/reference sections.
    old_generated = {s['heading']: s['content'].strip()
                     for s in sections(render_issue_body(before_action))}
    for name in ('入口、作用对象与行为边界', '竞品对标出处'):
        actual = by_heading[name][0]['content'].strip()
        if actual not in (old_generated[name], wanted[name]):
            archives.append('### 原' + name + '\n\n' + actual)

    patches = []
    for name in SPEC_HEADINGS:
        block = by_heading[name][0]
        patches.append((block['start'], block['end'], '## ' + name + '\n\n' + wanted[name] + '\n\n'))
    merged = old_body
    for start, end, replacement in sorted(patches, reverse=True):
        merged = merged[:start] + replacement + merged[end:]
    if archives:
        merged = merged.rstrip() + '\n\n' + ARCHIVE_HEADING + '\n\n'
        merged += '以下证据对应旧验收文字；新标准已重新置为待验证，历史记录不等于当前标准完成。\n\n'
        merged += '\n\n'.join('\n'.join('> ' + line for line in item.splitlines()) for item in archives)
        merged += '\n'
    return merged, {'fallback_archive': False, 'preserved_checks': preserved_checks,
                    'archived_blocks': len(archives),
                    'new_unchecked': len(set(action['criteria']) - old_texts)}


def save_preview(directory, aid, before, after):
    folder = directory / aid
    dump(folder / 'before.json', before)
    dump(folder / 'after.json', after)
    (folder / 'before.md').write_text(before.get('body') or '', encoding='utf-8')
    (folder / 'after.md').write_text(after['body'], encoding='utf-8')
    diff = difflib.unified_diff((before.get('body') or '').splitlines(keepends=True),
                               after['body'].splitlines(keepends=True),
                               fromfile=aid + '/before.md', tofile=aid + '/after.md')
    (folder / 'body.diff').write_text(''.join(diff), encoding='utf-8')


def plan_one(change, snapshot, mapping, directory, repo):
    aid = change['id']; action = change['after']; number = mapping[aid]['number']
    current = api('GET', f'repos/{repo}/issues/{number}')
    if not current['title'].startswith('[' + aid + ']'):
        raise ValueError('title ACTION-ID mismatch for #%d' % number)
    body, metrics = render_revision(action, change['before'], current.get('body') or '')
    payload = {'title': '[' + aid + '] ' + action['title'], 'body': body}
    save_preview(directory, aid, current, payload)
    # A second pass must produce exactly the same body before any remote edit.
    rerendered, _ = render_revision(action, change['before'], body)
    if rerendered != body:
        raise ValueError('merge is not idempotent for ' + aid)
    return {'id': aid, 'number': number, 'url': current['html_url'],
            'before_identity': body_identity(current), 'after': payload,
            'before_untouched': untouched_identity(current),
            'original_snapshot_changed': body_identity(snapshot[aid]) != body_identity(current),
            'needs_change': body_identity(current) != payload, 'metrics': metrics}


def apply_one(entry, directory, repo):
    aid = entry['id']; number = entry['number']; endpoint = f'repos/{repo}/issues/{number}'
    current = api('GET', endpoint)
    actual = body_identity(current)
    if actual == entry['after']:
        result = {'id': aid, 'number': number, 'status': 'already_current', 'verified_at': utc_now()}
        dump(directory / aid / 'verified.json', current)
        return result
    if actual != entry['before_identity']:
        dump(directory / aid / 'conflicting-current.json', current)
        raise RuntimeError('concurrent body/title edit; rerun dry-run to merge fresh evidence')
    untouched = untouched_identity(current)
    dump(directory / aid / 'apply-before.json', current)
    # No labels/state/assignees field is ever included in this PATCH.
    updated = api('PATCH', endpoint, entry['after'])
    dump(directory / aid / 'patch-response.json', updated)
    verified = api('GET', endpoint)
    dump(directory / aid / 'verified.json', verified)
    if body_identity(verified) != entry['after']:
        raise RuntimeError('post-PATCH title/body verification failed')
    if untouched_identity(verified) != untouched:
        raise RuntimeError('unrelated fields changed concurrently; preserved evidence requires review')
    return {'id': aid, 'number': number, 'status': 'updated', 'verified_at': utc_now(),
            'body_sha256': fingerprint(entry['after']['body']), 'preserved_unrelated_fields': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--dry-run', action='store_true')
    mode.add_argument('--apply', action='store_true')
    parser.add_argument('--changes', type=Path, default=ROOT / '.codex-work/audit-spec-changes.json')
    parser.add_argument('--snapshot', type=Path, default=ROOT / '.codex-work/issues-all.json')
    parser.add_argument('--output', type=Path, default=ROOT / '.codex-work/issue-revision-sync-2026-09-20')
    parser.add_argument('--workers', type=int, choices=range(1, 4), default=3)
    parser.add_argument('--repo', default=REPO)
    parser.add_argument('--ids', nargs='*', help='Only these ACTION-IDs from the change manifest')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    changes = read_json(args.changes)
    current_spec = {a['id']: a for a in load_actions()}
    if args.ids:
        wanted = set(args.ids)
        if not wanted <= {x['id'] for x in changes}:
            parser.error('unknown ACTION-ID in --ids')
        changes = [x for x in changes if x['id'] in wanted]
    for change in changes:
        if current_spec[change['id']] != change['after']:
            parser.error('manifest no longer matches tools/spec: ' + change['id'])
    manifest_hash = fingerprint(changes)
    plan_path = args.output / 'plan.json'
    results, failures = [], []
    if args.apply:
        plan = read_json(plan_path)
        if plan['repo'] != args.repo or plan['manifest_sha256'] != manifest_hash:
            parser.error('plan/manifest/repo mismatch; create a fresh dry-run plan first')
        entries = plan['entries']
        if plan['failures']:
            parser.error('dry-run has failures; resolve before apply')
        worker = lambda entry: apply_one(entry, args.output, args.repo)
    else:
        mapping = read_json(ROOT / 'docs/github/prd-issues.json')['issues']
        snapshot = {}
        for issue in read_json(args.snapshot):
            match = re.match(r'\[([A-Z]+-\d+)\]', issue['title'])
            if match:
                snapshot[match[1]] = issue
        entries = changes
        worker = lambda entry: plan_one(entry, snapshot, mapping, args.output, args.repo)
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(worker, entry): entry for entry in entries}
        for future in as_completed(futures):
            entry = futures[future]
            try:
                result = future.result(); results.append(result)
                print('%s %s' % (entry['id'], result.get('status', 'planned')), flush=True)
                if args.apply:
                    dump(args.output / entry['id'] / 'result.json', result)
            except Exception as exc:
                failed = {'id': entry['id'], 'status': 'failed', 'error': str(exc), 'at': utc_now()}
                failures.append(failed)
                dump(args.output / entry['id'] / ('result.json' if args.apply else 'plan-failure.json'), failed)
                print('%s FAILED: %s' % (entry['id'], exc), flush=True)
    order = {x['id']: n for n, x in enumerate(changes)}
    results.sort(key=lambda x: order[x['id']]); failures.sort(key=lambda x: order[x['id']])
    if args.apply:
        result = {'repo': args.repo, 'completed_at': utc_now(), 'results': results, 'failures': failures,
                  'counts': dict(collections.Counter(x['status'] for x in results + failures))}
        dump(args.output / 'result.json', result)
        print('Apply:', result['counts'], flush=True)
    else:
        plan = {'repo': args.repo, 'created_at': utc_now(), 'manifest_sha256': manifest_hash,
                'entries': results, 'failures': failures}
        dump(plan_path, plan)
        print('Dry-run: %d planned, %d need update, %d remote bodies/titles changed since snapshot, %d failed' % (
            len(results), sum(x['needs_change'] for x in results),
            sum(x['original_snapshot_changed'] for x in results), len(failures)), flush=True)
    return 1 if failures else 0


def self_test():
    import copy
    import unittest

    class Preservation(unittest.TestCase):
        def setUp(self):
            self.before = {'id': 'TXT-999', 'module': '文本比对', 'title': '测试',
                           'entry': '入口：A。\n作用对象：B。\n边界：C。',
                           'criteria': ['保持条件', '旧条件'], 'ref': '官方文档'}
            self.after = copy.deepcopy(self.before)
            self.after['criteria'] = ['保持条件', '新条件']
            self.after['entry'] = '入口：A。\n作用对象：B。\n边界：新。'
            self.body = render_issue_body(self.before)

        def merge(self, body=None):
            return render_revision(self.after, self.before, self.body if body is None else body)

        def test_preserves_unchanged_check_and_evidence(self):
            self.body = self.body.replace('- [ ] 保持条件', '- [x] 保持条件\n  → commit abc，已测。')
            body, metrics = self.merge()
            self.assertIn('- [x] 保持条件\n  → commit abc，已测。', body)
            self.assertEqual(metrics['preserved_checks'], 1)

        def test_changed_checked_criterion_is_reset_and_archived(self):
            self.body = self.body.replace('- [ ] 旧条件', '- [x] 旧条件\n  → 旧测试证据。')
            body, _ = self.merge()
            self.assertIn('- [ ] 新条件', body)
            self.assertNotIn('- [x] 新条件', body)
            self.assertIn('> - [x] 旧条件\n>   → 旧测试证据。', body)

        def test_unknown_sections_and_fenced_headings_are_preserved(self):
            evidence = '\n\n## 落地说明\n\ncommit def\n\n```md\n## 完成标准\nexample\n```\n'
            self.body += evidence
            body, _ = self.merge()
            self.assertIn(evidence, body)

        def test_idempotent_after_archiving(self):
            self.body = self.body.replace('- [ ] 旧条件', '- [x] 旧条件\n  人工证据')
            body, _ = self.merge()
            repeated, metrics = self.merge(body)
            self.assertEqual(body, repeated)
            self.assertEqual(metrics['archived_blocks'], 0)

        def test_unknown_inline_spec_notes_not_lost(self):
            self.body = self.body.replace('边界：C。', '边界：C。\n人工说明：请保留证据。')
            body, _ = self.merge()
            self.assertIn('人工说明：请保留证据。', body)

        def test_unstructured_body_fallback_retains_every_line(self):
            original = 'legacy\n- [x] approved\ncommit abc'
            body, metrics = self.merge(original)
            for line in original.splitlines():
                self.assertIn('> ' + line, body)
            self.assertTrue(metrics['fallback_archive'])
            repeated, _ = self.merge(body)
            self.assertEqual(body, repeated)

        def test_wrong_action_is_rejected(self):
            with self.assertRaises(ValueError):
                self.merge(self.body.replace('\nTXT-999\n', '\nTXT-998\n'))

        def test_unknown_extra_criteria_are_retained_as_history(self):
            self.body = self.body.replace('- [ ] 旧条件', '- [ ] 旧条件\n- [ ] 人工新增条件')
            body, _ = self.merge()
            self.assertIn('> - [ ] 人工新增条件', body)

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Preservation))
    if not result.wasSuccessful():
        raise SystemExit(1)


if __name__ == '__main__':
    raise SystemExit(main())
