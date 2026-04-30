"""
One-shot script: add new mic toggle translation entries to all .ts files
in app/languages/ under the SettingsView context.
Run from repo root.
"""
import os
import sys

LANGS_DIR = os.path.join(os.path.dirname(__file__), '..', 'app', 'languages')

NEW_MESSAGES = '''\
    <message>
        <location filename="../gui/SettingsView.qml" line="946"/>
        <source>Stream client microphone to host</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../gui/SettingsView.qml" line="956"/>
        <source>Send your microphone audio to the host. The host must be running a build that supports this. Takes effect at the start of the next stream.</source>
        <translation type="unfinished"></translation>
    </message>
'''

MARKER = '    <name>SettingsView</name>'
END_CONTEXT = '</context>'

def process_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Skip if already has our source string
    if 'Stream client microphone to host' in content:
        print(f'  SKIP (already present): {os.path.basename(path)}')
        return

    # Find the SettingsView context block
    settings_start = content.find(MARKER)
    if settings_start == -1:
        print(f'  WARN (no SettingsView context): {os.path.basename(path)}')
        return

    # Find the closing </context> after the SettingsView marker
    end_pos = content.find(END_CONTEXT, settings_start)
    if end_pos == -1:
        print(f'  WARN (no </context> after SettingsView): {os.path.basename(path)}')
        return

    # Insert the new messages just before </context>
    new_content = content[:end_pos] + NEW_MESSAGES + content[end_pos:]

    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print(f'  UPDATED: {os.path.basename(path)}')

def main():
    ts_files = sorted(f for f in os.listdir(LANGS_DIR) if f.endswith('.ts'))
    if not ts_files:
        print('No .ts files found!')
        sys.exit(1)
    print(f'Processing {len(ts_files)} .ts files...')
    for fname in ts_files:
        process_file(os.path.join(LANGS_DIR, fname))
    print('Done.')

if __name__ == '__main__':
    main()
