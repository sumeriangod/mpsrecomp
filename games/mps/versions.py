"""The known-good Mario Party Superstars v1.0.0 dump: identifiers of the player's own files, never the files.

setup.py (next to this) checks a dump against these before anything is built from it; the launcher
(runtime/ui/launcher.mm) runs setup.py. NSO build ids name a build of the code (the recomp's addresses and findings
are for these); SHA-256 of the extracted ExeFS files are the same for every 1.0.0 dump.
Taken from the owner's dump (2026-11-25). The recomp is of 1.0.0, the base game itself: an update isn't used.
"""

TITLE = 'Mario Party Superstars'
VERSION = '1.0.0'
BASE_TITLE_ID = 0x01006FE013472000
UPDATE_TITLE_ID = None
UPDATE_VERSION = None
TITLE_VERSIONS = {
    (BASE_TITLE_ID, 0): '1.0.0',
}

BUILD_IDS = {
    '1.0.0': {
        'main': '0413a570bf2b3aa7741c20559ff7b8fa314adbf9',
        'subsdk0': '4dd097b435891c5e0e15a65dbe47124f4d7fd1c1',
        'subsdk1': '507f19d1240d09d00992248e8a0fd77ebd784eb4',
        'sdk': '2d369d9999e3e10e665106f840ffff9bdaa0bb8e',
    },
}

EXEFS_SHA256 = {
    'main': 'be53ba900360245479a1df24c9c8d1a8b5ecb59e1b22d837399efc276ee6821f',
    'subsdk0': 'c0a7a238688caf191c7c40badcbeac4d037a95ccaed644ce7fe2e63b5f898a13',
    'subsdk1': '67a38fcf89cd13e040a021eb3d157072f38ebf6680a907874c35c7b231b5d4e2',
    'sdk': '1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017',
    'rtld': 'bc175ad9865adb48f9cbe9d7993ddb3367d3771bc06642c87d16ccf4f4dc9ff4',
    'main.npdm': '9657b5a34bf62748f27a9fcf8f6332402d6d25ed086c4c46c3f505a20c0af6b4',
}

PROGRAM_NCA_IDS = {}

EXEFS_FILES = ('main', 'subsdk0', 'subsdk1', 'sdk', 'rtld', 'main.npdm')
ROMFS_MARKERS = ('Archive', 'nro', 'stream')  # (top-level folders of its RomFS)
ROMFS_MIN_FILES = 500  # (814 in 1.0.0)


def version_of_build_id(module, build_id):
    """'1.0.0', or None for a build id of another version (or another game)."""
    for ver, ids in BUILD_IDS.items():
        want = ids.get(module)
        if want and build_id and build_id.startswith(want):
            return ver
    return None


def display_version(title_id, v):
    """A title version by its name ('1.0.0'), or its number for an unknown one ('v65536')."""
    return TITLE_VERSIONS.get((title_id, v), f'v{v}')
